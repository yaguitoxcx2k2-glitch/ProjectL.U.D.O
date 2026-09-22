#include "BattleDialog.h"

#include "core/Editor.h"
#include "game/RpgSystem.h"
#include "game/GamepadInput.h"

#include <QBrush>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QVBoxLayout>

namespace game {
namespace {

QStringList idList(const QVariant& value)
{
    QStringList result = value.toStringList();
    if (!result.isEmpty()) return result;
    for (const QVariant& entry : value.toList()) {
        const QString id = entry.toString();
        if (!id.isEmpty()) result.push_back(id);
    }
    if (result.isEmpty() && value.canConvert<QString>()) {
        for (const QString& part : value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            result.push_back(part.trimmed());
    }
    return result;
}

} // namespace

BattleDialog::BattleDialog(const core::Editor& ed, GameState& state, const QString& troopId,
                           bool allowEscape, QWidget* parent)
    : QDialog(parent), m_ed(ed), m_state(state), m_allowEscape(allowEscape)
{
    setWindowTitle(tr("Batalha"));
    resize(900, 650);
    setModal(true);
    const core::DatabaseRecord* troop = databaseRecord(ed, QStringLiteral("troops"), troopId);
    if (!troop) {
        m_validationError = tr("A tropa selecionada não existe no Banco de Dados.");
        return;
    }
    for (const QString& enemyId : idList(troop->data.value(QStringLiteral("enemyIds")))) {
        const core::DatabaseRecord* record = databaseRecord(ed, QStringLiteral("enemies"), enemyId);
        if (!record) continue;
        Enemy enemy;
        enemy.recordId = record->id;
        enemy.name = record->name.isEmpty() ? tr("Inimigo") : record->name;
        enemy.graphicPath = record->data.value(QStringLiteral("graphicPath")).toString();
        enemy.maxHp = qMax(1, record->data.value(QStringLiteral("hp"), 100).toInt());
        enemy.hp = enemy.maxHp;
        enemy.maxMp = qMax(0, record->data.value(QStringLiteral("mp"), 0).toInt());
        enemy.mp = enemy.maxMp;
        enemy.attack = qMax(1, record->data.value(QStringLiteral("attack"), 10).toInt());
        enemy.defense = qMax(0, record->data.value(QStringLiteral("defense"), 5).toInt());
        enemy.agility = qMax(1, record->data.value(QStringLiteral("agility"), 8).toInt());
        enemy.hitRate = qBound(1, record->data.value(QStringLiteral("hitRate"),95).toInt(),100);
        enemy.evasion = qBound(0, record->data.value(QStringLiteral("evasion"),5).toInt(),95);
        enemy.critical = qBound(0, record->data.value(QStringLiteral("critical"),5).toInt(),100);
        enemy.skillIds = idList(record->data.value(QStringLiteral("skillIds")));
        enemy.experience = qMax(0, record->data.value(QStringLiteral("experience"), 0).toInt());
        enemy.gold = qMax(0, record->data.value(QStringLiteral("gold"), 0).toInt());
        enemy.lootId = record->data.value(QStringLiteral("lootId")).toString();
        enemy.lootChance = qBound(0, record->data.value(QStringLiteral("lootChance"), 0).toInt(), 100);
        m_enemies.push_back(enemy);
    }
    if (m_enemies.isEmpty()) {
        m_validationError = tr("A tropa não possui inimigos válidos.");
        return;
    }
    if (m_state.party().isEmpty()) {
        m_validationError = tr("O grupo do jogador está vazio.");
        return;
    }
    bool hasLivingActor = false;
    for (const PartyMemberState& member : m_state.party())
        if (member.hp > 0) { hasLivingActor = true; break; }
    if (!hasLivingActor) {
        m_validationError = tr("Todos os personagens do grupo estão derrotados.");
        return;
    }
    m_valid = true;

    auto* root = new QVBoxLayout(this);
    QLabel* background = new QLabel(this);
    background->setMinimumHeight(180);
    background->setAlignment(Qt::AlignCenter);
    background->setStyleSheet(QStringLiteral("background:#172033;border:1px solid #526070;color:#dce7f5;font-size:24px;font-weight:700"));
    const core::MapDoc* map = ed.doc();
    const QString backgroundPath = map ? map->environment.battleBackgroundPath : QString();
    QPixmap backgroundPixmap;
    if (!backgroundPath.isEmpty())
        backgroundPixmap.load(QFileInfo(backgroundPath).isAbsolute()
                                  ? backgroundPath : QDir(ed.projectRoot()).filePath(backgroundPath));
    if (!backgroundPixmap.isNull())
        background->setPixmap(backgroundPixmap.scaled(860, 220, Qt::KeepAspectRatioByExpanding,
                                                       Qt::SmoothTransformation));
    else
        background->setText(troop->name.isEmpty() ? tr("BATALHA") : troop->name);
    auto* enemyScene=new QHBoxLayout(background);enemyScene->setContentsMargins(28,18,28,18);enemyScene->addStretch(1);
    for(const Enemy& enemy:m_enemies){auto* sprite=new QLabel(background);sprite->setAlignment(Qt::AlignCenter);sprite->setMinimumSize(100,130);QPixmap image;if(!enemy.graphicPath.isEmpty())image.load(QFileInfo(enemy.graphicPath).isAbsolute()?enemy.graphicPath:QDir(ed.projectRoot()).filePath(enemy.graphicPath));if(!image.isNull())sprite->setPixmap(image.scaled(150,150,Qt::KeepAspectRatio,Qt::SmoothTransformation));else{sprite->setText(enemy.name);sprite->setStyleSheet(QStringLiteral("background:rgba(12,18,28,185);border:1px solid #6b7c93;border-radius:8px;color:white;font-weight:700"));}m_enemySprites.push_back(sprite);enemyScene->addWidget(sprite,0,Qt::AlignBottom);}enemyScene->addStretch(1);
    root->addWidget(background);
    m_turnLabel = new QLabel(this);
    m_turnLabel->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;padding:4px"));
    root->addWidget(m_turnLabel);

    auto* combatRow = new QHBoxLayout;
    m_enemyList = new QListWidget(this);
    m_partyList = new QListWidget(this);
    combatRow->addWidget(m_enemyList, 1);
    combatRow->addWidget(m_partyList, 1);
    root->addLayout(combatRow, 1);
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(105);
    root->addWidget(m_log);

    auto* commands = new QHBoxLayout;
    m_attackButton = new QPushButton(tr("Atacar"), this);
    m_skillButton = new QPushButton(tr("Habilidade"), this);
    m_itemButton = new QPushButton(tr("Item"), this);
    m_defendButton = new QPushButton(tr("Defender"), this);
    m_escapeButton = new QPushButton(tr("Fugir"), this);
    m_escapeButton->setEnabled(m_allowEscape);
    for (QPushButton* button : {m_attackButton, m_skillButton, m_itemButton,
                                m_defendButton, m_escapeButton})
        commands->addWidget(button);
    root->addLayout(commands);
    connect(m_attackButton, &QPushButton::clicked, this, &BattleDialog::attack);
    connect(m_skillButton, &QPushButton::clicked, this, &BattleDialog::useSkill);
    connect(m_itemButton, &QPushButton::clicked, this, &BattleDialog::useItem);
    connect(m_defendButton, &QPushButton::clicked, this, &BattleDialog::defend);
    connect(m_escapeButton, &QPushButton::clicked, this, &BattleDialog::tryEscape);

    while (m_actorTurn < m_state.party().size() && m_state.party()[m_actorTurn].hp <= 0) ++m_actorTurn;
    rebuildLists();
    updateTurnLabel();
    appendLog(tr("%1 apareceu!").arg(troop->name));
    m_enemyList->setFocus();
    new GamepadDialogNavigator(this, m_ed.inputSystem);
}

void BattleDialog::reject()
{
    if (!m_valid) { QDialog::reject(); return; }
    if (m_allowEscape) tryEscape();
}

void BattleDialog::appendLog(const QString& text)
{
    m_log->appendPlainText(text);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

PartyMemberState* BattleDialog::currentActor()
{
    if (m_actorTurn < 0 || m_actorTurn >= m_state.party().size()) return nullptr;
    return &m_state.party()[m_actorTurn];
}

BattleDialog::Enemy* BattleDialog::selectedEnemy()
{
    int index = m_enemyList->currentRow();
    if (index >= 0 && index < m_enemies.size() && m_enemies[index].hp > 0) return &m_enemies[index];
    for (int i = 0; i < m_enemies.size(); ++i)
        if (m_enemies[i].hp > 0) { m_enemyList->setCurrentRow(i); return &m_enemies[i]; }
    return nullptr;
}

bool BattleDialog::allEnemiesDefeated() const
{
    for (const Enemy& enemy : m_enemies) if (enemy.hp > 0) return false;
    return true;
}

bool BattleDialog::allPartyDefeated() const
{
    for (const PartyMemberState& member : m_state.party()) if (member.hp > 0) return false;
    return true;
}

void BattleDialog::rebuildLists()
{
    const int selected = m_enemyList->currentRow();
    m_enemyList->clear();
    for (int enemyIndex=0;enemyIndex<m_enemies.size();++enemyIndex) {
        const Enemy& enemy=m_enemies[enemyIndex];
        auto* item = new QListWidgetItem(enemy.hp > 0
            ? tr("%1\nHP %2 / %3").arg(enemy.name).arg(enemy.hp).arg(enemy.maxHp)
            : tr("%1\nDerrotado").arg(enemy.name), m_enemyList);
        if (enemy.hp <= 0) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        if(enemyIndex<m_enemySprites.size())m_enemySprites[enemyIndex]->setVisible(enemy.hp>0);
    }
    m_enemyList->setCurrentRow(qBound(0, selected, qMax(0, m_enemyList->count() - 1)));
    selectedEnemy();
    m_partyList->clear();
    for (const PartyMemberState& member : m_state.party()) {
        const CombatStats stats = memberStats(m_ed, member);
        const QString name = databaseRecordName(m_ed, QStringLiteral("actors"), member.actorId, tr("Personagem"));
        auto* item = new QListWidgetItem(tr("%1   Nv %2\nHP %3/%4   MP %5/%6")
                                             .arg(name).arg(member.level).arg(member.hp).arg(stats.maxHp)
                                             .arg(member.mp).arg(stats.maxMp), m_partyList);
        if (member.hp <= 0) item->setForeground(QBrush(QColor("#888888")));
    }
    if (m_actorTurn >= 0 && m_actorTurn < m_partyList->count()) m_partyList->setCurrentRow(m_actorTurn);
}

void BattleDialog::updateTurnLabel()
{
    const PartyMemberState* actor = currentActor();
    const QString name = actor
        ? databaseRecordName(m_ed, QStringLiteral("actors"), actor->actorId, tr("Personagem"))
        : tr("Inimigos");
    m_turnLabel->setText(tr("Turno de %1").arg(name));
}

void BattleDialog::attack()
{
    PartyMemberState* actor = currentActor();
    Enemy* enemy = selectedEnemy();
    if (!actor || !enemy || actor->hp <= 0) return;
    const CombatStats stats = memberStats(m_ed, *actor);
    const core::DatabaseRecord* actorRecord=databaseRecord(m_ed,QStringLiteral("actors"),actor->actorId);
    const int hit=qBound(1,actorRecord?actorRecord->data.value(QStringLiteral("hitRate"),95).toInt():95,100);
    if(QRandomGenerator::global()->bounded(100)>=qBound(5,hit-enemy->evasion,100)){
        appendLog(tr("%1 errou o ataque em %2.").arg(databaseRecordName(m_ed,QStringLiteral("actors"),actor->actorId,tr("Personagem")),enemy->name));finishPlayerAction();return;
    }
    const int variance = QRandomGenerator::global()->bounded(5) - 2;
    int damage = qMax(1, stats.attack - enemy->defense / 2 + variance);
    const int critical=qBound(0,actorRecord?actorRecord->data.value(QStringLiteral("critical"),5).toInt():5,100);
    const bool criticalHit=QRandomGenerator::global()->bounded(100)<critical;if(criticalHit)damage=qMax(1,damage*2);
    enemy->hp = qMax(0, enemy->hp - damage);
    appendLog(tr("%1 atacou %2: %3 de dano.%4")
                  .arg(databaseRecordName(m_ed, QStringLiteral("actors"), actor->actorId, tr("Personagem")),
                       enemy->name).arg(damage).arg(criticalHit?tr(" Crítico!"):QString()));
    finishPlayerAction();
}

void BattleDialog::useSkill()
{
    PartyMemberState* actor = currentActor();
    Enemy* enemy = selectedEnemy();
    if (!actor || !enemy) return;
    QStringList skillIds;
    if (const core::DatabaseRecord* record = databaseRecord(m_ed, QStringLiteral("actors"), actor->actorId)) {
        skillIds += idList(record->data.value(QStringLiteral("skillIds")));
        if (const core::DatabaseRecord* klass = databaseRecord(
                m_ed, QStringLiteral("classes"), record->data.value(QStringLiteral("classId")).toString()))
            skillIds += idList(klass->data.value(QStringLiteral("skillIds")));
    }
    skillIds.removeDuplicates();
    QStringList labels;
    QVector<const core::DatabaseRecord*> skills;
    for (const QString& id : skillIds) {
        const core::DatabaseRecord* skill = databaseRecord(m_ed, QStringLiteral("skills"), id);
        if (!skill) continue;
        labels.push_back(tr("%1 (MP %2)").arg(skill->name).arg(skill->data.value(QStringLiteral("mpCost")).toInt()));
        skills.push_back(skill);
    }
    if (skills.isEmpty()) {
        QMessageBox::information(this, tr("Habilidade"), tr("Este personagem não possui habilidades configuradas."));
        return;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Habilidade"), tr("Escolha:"), labels, 0, false, &ok);
    if (!ok) return;
    const core::DatabaseRecord* skill = skills[labels.indexOf(chosen)];
    const int cost = qMax(0, skill->data.value(QStringLiteral("mpCost")).toInt());
    if (actor->mp < cost) {
        QMessageBox::information(this, tr("Habilidade"), tr("MP insuficiente."));
        return;
    }
    const QString scope = skill->data.value(QStringLiteral("scope"), QStringLiteral("enemyOne")).toString();
    const int healHp = skill->data.value(QStringLiteral("healHp")).toInt();
    const int healMp = skill->data.value(QStringLiteral("healMp")).toInt();
    QVector<PartyMemberState*> allies;
    if (scope == QLatin1String("self")) allies.push_back(actor);
    else if (scope == QLatin1String("allyAll")) {
        for (PartyMemberState& member : m_state.party()) allies.push_back(&member);
    } else if (scope == QLatin1String("allyOne")) {
        QStringList actorLabels;
        QVector<PartyMemberState*> candidates;
        for (PartyMemberState& member : m_state.party()) {
            actorLabels.push_back(databaseRecordName(m_ed, QStringLiteral("actors"), member.actorId, tr("Personagem")));
            candidates.push_back(&member);
        }
        bool targetOk = false;
        const QString targetName = QInputDialog::getItem(this, tr("Alvo"), tr("Escolha o aliado:"),
                                                          actorLabels, 0, false, &targetOk);
        if (!targetOk) return;
        allies.push_back(candidates[actorLabels.indexOf(targetName)]);
    }
    actor->mp -= cost;
    const QString stateToAdd=skill->data.value(QStringLiteral("stateAddId")).toString();
    const QString stateToRemove=skill->data.value(QStringLiteral("stateRemoveId")).toString();
    if (!allies.isEmpty() && (healHp != 0 || healMp != 0 ||
                              !stateToAdd.isEmpty() || !stateToRemove.isEmpty())) {
        for (PartyMemberState* target : allies) {
            const CombatStats stats = memberStats(m_ed, *target);
            target->hp = qBound(0, target->hp + healHp, stats.maxHp);
            target->mp = qBound(0, target->mp + healMp, stats.maxMp);
            addState(*target,stateToAdd,skill->data.value(QStringLiteral("stateChance"),100).toInt());
            if(!stateToRemove.isEmpty()){target->states.removeAll(stateToRemove);target->stateTurns.remove(stateToRemove);}
        }
        appendLog(tr("%1 usou %2 no grupo.")
                      .arg(databaseRecordName(m_ed, QStringLiteral("actors"), actor->actorId, tr("Personagem")),
                           skill->name));
    } else {
        const CombatStats stats = memberStats(m_ed, *actor);
        const int hitRate=qBound(1,skill->data.value(QStringLiteral("hitRate"),100).toInt(),100);
        const int criticalChance=qBound(0,skill->data.value(QStringLiteral("critical"),0).toInt(),100);
        const QString element=skill->data.value(QStringLiteral("element")).toString();
        auto damageTarget=[&](Enemy& target){
            if(QRandomGenerator::global()->bounded(100)>=qBound(5,hitRate-target.evasion,100))return -1;
            int damage=qMax(1,stats.attack/2+skill->data.value(QStringLiteral("power"),10).toInt()-target.defense/3);
            if(!element.isEmpty())if(const core::DatabaseRecord* enemyRecord=databaseRecord(m_ed,QStringLiteral("enemies"),target.recordId))
                damage=qMax(0,qRound(damage*qBound(0.0,enemyRecord->data.value(QStringLiteral("elementRates")).toMap().value(element,1.0).toDouble(),10.0)));
            if(QRandomGenerator::global()->bounded(100)<criticalChance)damage*=2;
            target.hp=qMax(0,target.hp-damage);
            addState(target,skill->data.value(QStringLiteral("stateAddId")).toString(),skill->data.value(QStringLiteral("stateChance"),100).toInt());
            return damage;
        };
        if (scope == QLatin1String("enemyAll")) {
            int total=0,hits=0;for(Enemy& target:m_enemies)if(target.hp>0){const int dealt=damageTarget(target);if(dealt>=0){total+=dealt;++hits;}}
            appendLog(tr("%1 usou %2 em todos os inimigos: %3 acerto(s), %4 de dano total.")
                          .arg(databaseRecordName(m_ed, QStringLiteral("actors"), actor->actorId, tr("Personagem")),
                               skill->name).arg(hits).arg(total));
        } else {
            const int damage=damageTarget(*enemy);
            appendLog(damage<0?tr("%1 usou %2, mas errou %3.").arg(databaseRecordName(m_ed,QStringLiteral("actors"),actor->actorId,tr("Personagem")),skill->name,enemy->name):tr("%1 usou %2 em %3: %4 de dano.")
                          .arg(databaseRecordName(m_ed, QStringLiteral("actors"), actor->actorId, tr("Personagem")),
                               skill->name, enemy->name).arg(damage));
        }
    }
    finishPlayerAction();
}

void BattleDialog::useItem()
{
    QStringList labels;
    QVector<const core::DatabaseRecord*> items;
    const auto itemCategory = m_ed.database.constFind(QStringLiteral("items"));
    if (itemCategory != m_ed.database.cend())
    for (const core::DatabaseRecord& item : itemCategory.value()) {
        if (m_state.itemCount(item.id) <= 0) continue;
        if (item.data.value(QStringLiteral("healHp")).toInt() == 0 &&
            item.data.value(QStringLiteral("healMp")).toInt() == 0 &&
            item.data.value(QStringLiteral("stateAddId")).toString().isEmpty() &&
            item.data.value(QStringLiteral("stateRemoveId")).toString().isEmpty()) continue;
        labels.push_back(tr("%1 ×%2").arg(item.name).arg(m_state.itemCount(item.id)));
        items.push_back(&item);
    }
    if (items.isEmpty()) {
        QMessageBox::information(this, tr("Item"), tr("Não há itens utilizáveis."));
        return;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Item"), tr("Escolha:"), labels, 0, false, &ok);
    if (!ok) return;
    const core::DatabaseRecord* item = items[labels.indexOf(chosen)];
    QVector<PartyMemberState*> targets;
    const QString scope = item->data.value(QStringLiteral("scope"), QStringLiteral("allyOne")).toString();
    if (scope == QLatin1String("allyAll")) {
        for (PartyMemberState& member : m_state.party()) targets.push_back(&member);
    } else {
        QStringList actorLabels;
        QVector<PartyMemberState*> candidates;
        for (PartyMemberState& member : m_state.party()) {
            actorLabels.push_back(databaseRecordName(m_ed, QStringLiteral("actors"), member.actorId, tr("Personagem")));
            candidates.push_back(&member);
        }
        bool targetOk = false;
        const QString targetName = QInputDialog::getItem(this, tr("Alvo"), tr("Escolha o aliado:"),
                                                          actorLabels, 0, false, &targetOk);
        if (!targetOk) return;
        targets.push_back(candidates[actorLabels.indexOf(targetName)]);
    }
    for (PartyMemberState* target : targets) {
        const CombatStats stats = memberStats(m_ed, *target);
        target->hp = qBound(0, target->hp + item->data.value(QStringLiteral("healHp")).toInt(), stats.maxHp);
        target->mp = qBound(0, target->mp + item->data.value(QStringLiteral("healMp")).toInt(), stats.maxMp);
        addState(*target,item->data.value(QStringLiteral("stateAddId")).toString(),
                 item->data.value(QStringLiteral("stateChance"),100).toInt());
        const QString removeState=item->data.value(QStringLiteral("stateRemoveId")).toString();
        if(!removeState.isEmpty()){target->states.removeAll(removeState);target->stateTurns.remove(removeState);}
    }
    if (item->data.value(QStringLiteral("consumable"), true).toBool()) m_state.addItem(item->id, -1);
    appendLog(tr("%1 usou %2.")
                  .arg(databaseRecordName(m_ed, QStringLiteral("actors"), currentActor()->actorId, tr("Personagem")),
                       item->name));
    finishPlayerAction();
}

void BattleDialog::defend()
{
    PartyMemberState* actor = currentActor();
    if (!actor) return;
    m_defending.insert(actor->actorId);
    appendLog(tr("%1 está defendendo.")
                  .arg(databaseRecordName(m_ed, QStringLiteral("actors"), actor->actorId, tr("Personagem"))));
    finishPlayerAction();
}

void BattleDialog::tryEscape()
{
    if (!m_allowEscape) return;
    if (QRandomGenerator::global()->bounded(100) < 65) {
        m_result = BattleResult::Escaped;
        done(int(m_result));
        return;
    }
    appendLog(tr("A fuga falhou!"));
    enemyTurn();
}

void BattleDialog::finishPlayerAction()
{
    rebuildLists();
    if (allEnemiesDefeated()) { finishVictory(); return; }
    int next = m_actorTurn + 1;
    while (next < m_state.party().size() && m_state.party()[next].hp <= 0) ++next;
    if (next < m_state.party().size()) {
        m_actorTurn = next;
        rebuildLists();
        updateTurnLabel();
        return;
    }
    enemyTurn();
}

void BattleDialog::enemyTurn()
{
    QVector<int> alive;
    for (int index = 0; index < m_state.party().size(); ++index)
        if (m_state.party()[index].hp > 0) alive.push_back(index);
    for (Enemy& enemy : m_enemies) {
        if (enemy.hp <= 0 || alive.isEmpty()) continue;
        QVector<const core::DatabaseRecord*> skills;
        for(const QString& id:enemy.skillIds)if(const core::DatabaseRecord* skill=databaseRecord(m_ed,QStringLiteral("skills"),id))
            if(enemy.mp>=qMax(0,skill->data.value(QStringLiteral("mpCost")).toInt())&&QRandomGenerator::global()->bounded(100)<qBound(0,skill->data.value(QStringLiteral("useChance"),100).toInt(),100))skills.push_back(skill);
        const int targetIndex = alive[QRandomGenerator::global()->bounded(int(alive.size()))];
        PartyMemberState& target = m_state.party()[targetIndex];
        const CombatStats stats = memberStats(m_ed, target);
        const core::DatabaseRecord* skill=skills.isEmpty()?nullptr:skills[QRandomGenerator::global()->bounded(int(skills.size()))];
        const QString skillScope=skill?skill->data.value(QStringLiteral("scope"),QStringLiteral("enemyOne")).toString():QStringLiteral("enemyOne");
        if(skill&&(skillScope==QLatin1String("self")||skillScope==QLatin1String("allyOne")||skillScope==QLatin1String("allyAll"))){
            enemy.mp-=qMax(0,skill->data.value(QStringLiteral("mpCost")).toInt());QVector<Enemy*> recipients;
            if(skillScope==QLatin1String("allyAll")){for(Enemy& ally:m_enemies)if(ally.hp>0)recipients.push_back(&ally);}else recipients.push_back(&enemy);
            for(Enemy* ally:recipients){ally->hp=qBound(0,ally->hp+skill->data.value(QStringLiteral("healHp")).toInt(),ally->maxHp);ally->mp=qBound(0,ally->mp+skill->data.value(QStringLiteral("healMp")).toInt(),ally->maxMp);addState(*ally,skill->data.value(QStringLiteral("stateAddId")).toString(),skill->data.value(QStringLiteral("stateChance"),100).toInt());const QString remove=skill->data.value(QStringLiteral("stateRemoveId")).toString();if(!remove.isEmpty())ally->stateTurns.remove(remove);}
            appendLog(tr("%1 usou %2 no próprio grupo.").arg(enemy.name,skill->name));continue;
        }
        const int hitRate=skill?qBound(1,skill->data.value(QStringLiteral("hitRate"),enemy.hitRate).toInt(),100):enemy.hitRate;
        const core::DatabaseRecord* actorRecord=databaseRecord(m_ed,QStringLiteral("actors"),target.actorId);
        const int evasion=qBound(0,actorRecord?actorRecord->data.value(QStringLiteral("evasion"),5).toInt():5,95);
        if(QRandomGenerator::global()->bounded(100)>=qBound(5,hitRate-evasion,100)){appendLog(tr("%1 errou %2.").arg(enemy.name,databaseRecordName(m_ed,QStringLiteral("actors"),target.actorId,tr("Personagem"))));continue;}
        if(skill)enemy.mp-=qMax(0,skill->data.value(QStringLiteral("mpCost")).toInt());
        int damage = qMax(1, enemy.attack - stats.defense / 2 + QRandomGenerator::global()->bounded(5) - 2);
        if(skill)damage=qMax(1,enemy.attack/2+skill->data.value(QStringLiteral("power"),10).toInt()-stats.defense/3);
        if(skill){const QString element=skill->data.value(QStringLiteral("element")).toString();if(!element.isEmpty()&&actorRecord)damage=qMax(0,qRound(damage*qBound(0.0,actorRecord->data.value(QStringLiteral("elementRates")).toMap().value(element,1.0).toDouble(),10.0)));}
        const int criticalChance=skill?qBound(0,skill->data.value(QStringLiteral("critical"),enemy.critical).toInt(),100):enemy.critical;
        const bool criticalHit=QRandomGenerator::global()->bounded(100)<criticalChance;if(criticalHit)damage*=2;
        if (m_defending.contains(target.actorId)) damage = qMax(1, damage / 2);
        target.hp = qMax(0, target.hp - damage);
        if(skill)addState(target,skill->data.value(QStringLiteral("stateAddId")).toString(),skill->data.value(QStringLiteral("stateChance"),100).toInt());
        appendLog(tr("%1 usou %2 em %3: %4 de dano.%5")
                      .arg(enemy.name,
                           skill?skill->name:tr("Ataque"),
                           databaseRecordName(m_ed, QStringLiteral("actors"), target.actorId, tr("Personagem")))
                      .arg(damage).arg(criticalHit?tr(" Crítico!"):QString()));
        if (target.hp <= 0) alive.removeAll(targetIndex);
    }
    m_defending.clear();
    finishRoundStates();
    rebuildLists();
    if (allEnemiesDefeated()) { finishVictory(); return; }
    if (allPartyDefeated()) { finishDefeat(); return; }
    m_actorTurn = 0;
    while (m_actorTurn < m_state.party().size() && m_state.party()[m_actorTurn].hp <= 0) ++m_actorTurn;
    rebuildLists();
    updateTurnLabel();
}

void BattleDialog::addState(PartyMemberState& target,const QString& stateId,int chance)
{
    const core::DatabaseRecord* state=databaseRecord(m_ed,QStringLiteral("states"),stateId);if(!state||QRandomGenerator::global()->bounded(100)>=qBound(0,chance,100))return;
    if(!target.states.contains(stateId))target.states.push_back(stateId);target.stateTurns[stateId]=qMax(0,state->data.value(QStringLiteral("duration"),0).toInt());
}

void BattleDialog::addState(Enemy& target,const QString& stateId,int chance)
{
    const core::DatabaseRecord* state=databaseRecord(m_ed,QStringLiteral("states"),stateId);if(!state||QRandomGenerator::global()->bounded(100)>=qBound(0,chance,100))return;
    target.stateTurns[stateId]=qMax(0,state->data.value(QStringLiteral("duration"),0).toInt());
}

void BattleDialog::finishRoundStates()
{
    auto process=[this](auto& target,int maxHp,const QString& name){
        QStringList remove;for(auto it=target.stateTurns.begin();it!=target.stateTurns.end();++it){const core::DatabaseRecord* state=databaseRecord(m_ed,QStringLiteral("states"),it.key());if(!state){remove.push_back(it.key());continue;}const int rate=qBound(0,state->data.value(QStringLiteral("hpDamageRate"),0).toInt(),100);if(rate>0&&target.hp>0){const int damage=qMax(1,maxHp*rate/100);target.hp=qMax(0,target.hp-damage);appendLog(tr("%1 sofreu %2 de dano por %3.").arg(name).arg(damage).arg(state->name));}if(it.value()>0&&--it.value()<=0)remove.push_back(it.key());}for(const QString& id:remove)target.stateTurns.remove(id);
    };
    for(PartyMemberState& actor:m_state.party()){process(actor,memberStats(m_ed,actor).maxHp,databaseRecordName(m_ed,QStringLiteral("actors"),actor.actorId,tr("Personagem")));for(int i=actor.states.size()-1;i>=0;--i)if(!actor.stateTurns.contains(actor.states[i]))actor.states.removeAt(i);}
    for(Enemy& enemy:m_enemies)process(enemy,enemy.maxHp,enemy.name);
    ++m_round;
}

void BattleDialog::finishVictory()
{
    int experience = 0;
    int gold = 0;
    QStringList lootNames;
    QStringList levelMessages;
    for (const Enemy& enemy : m_enemies) {
        experience += enemy.experience;
        gold += enemy.gold;
        if (!enemy.lootId.isEmpty() && QRandomGenerator::global()->bounded(100) < enemy.lootChance) {
            m_state.addItem(enemy.lootId, 1);
            QString category;
            const core::DatabaseRecord* item = nullptr;
            for (const QString& cat : {QStringLiteral("items"), QStringLiteral("weapons"), QStringLiteral("armors")})
                if ((item = databaseRecord(m_ed, cat, enemy.lootId))) break;
            lootNames.push_back(item ? item->name : enemy.lootId);
        }
    }
    m_state.addGold(gold);
    for (PartyMemberState& member : m_state.party()) gainExperience(m_ed, member, experience, &levelMessages);
    QString summary = tr("Vitória!\n%1 EXP\n%2 G").arg(experience).arg(gold);
    if (!lootNames.isEmpty()) summary += tr("\nItens: %1").arg(lootNames.join(QStringLiteral(", ")));
    if (!levelMessages.isEmpty()) summary += QLatin1Char('\n') + levelMessages.join(QLatin1Char('\n'));
    QMessageBox::information(this, tr("Vitória"), summary);
    m_result = BattleResult::Victory;
    done(int(m_result));
}

void BattleDialog::finishDefeat()
{
    m_result = BattleResult::Defeat;
    done(int(m_result));
}

} // namespace game
