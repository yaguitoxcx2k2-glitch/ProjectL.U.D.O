#include "UiBattleController.h"

#include "core/Editor.h"
#include "game/RpgSystem.h"
#include "game/ui/UiDataBinding.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QtMath>

namespace game::ui {
namespace {
QString bt(const char* s) { return QCoreApplication::translate("UiBattleController", s); }
QString battleTr(const core::Editor& ed, const QString& key, const char* fallback)
{
    return core::resolvePlayerText(ed.localization, key, bt(fallback));
}

bool initialScreenStateEnabled(const core::Editor& ed, const QString& screen, const QString& elementId)
{
    for (auto it = ed.gameUi.screenStates.cbegin(); it != ed.gameUi.screenStates.cend(); ++it) {
        if (!it.value().initial || it.value().screen != screen) continue;
        const auto ov = it.value().elements.constFind(elementId);
        return ov == it.value().elements.cend() || !ov.value().hasEnabled || ov.value().enabled;
    }
    return true;
}

QStringList idList(const QVariant& value)
{
    QStringList result = value.toStringList();
    if (!result.isEmpty()) return result;
    for (const QVariant& entry : value.toList()) {
        const QString id = entry.toString();
        if (!id.isEmpty()) result.push_back(id);
    }
    if (result.isEmpty() && value.canConvert<QString>())
        for (const QString& part : value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            result.push_back(part.trimmed());
    return result;
}

QVector<int> livingPartyIndices(const GameState& state)
{
    QVector<int> out;
    for (int i = 0; i < state.party().size(); ++i) if (state.party()[i].hp > 0) out.push_back(i);
    return out;
}
}

UiBattleController::UiBattleController(const core::Editor& editor, GameState& state)
    : m_ed(editor), m_state(state) {}

bool UiBattleController::open(const QString& troopId, bool allowEscape, QString* error)
{
    clear();
    m_allowEscape = allowEscape;
    m_troopId = troopId;
    const core::DatabaseRecord* troop = databaseRecord(m_ed, QStringLiteral("troops"), troopId);
    if (!troop) {
        if (error) *error = bt("A tropa selecionada não existe no Banco de Dados.");
        return false;
    }
    m_troopName = databaseRecordName(m_ed, QStringLiteral("troops"), troop->id, bt("Batalha"));
    m_battleEvents = battleEventRules(troop);

    QVector<TroopEnemySlot> enemySlots = troopEnemySlots(troop);
    if (enemySlots.isEmpty()) {
        const QStringList legacyIds = idList(troop->data.value(QStringLiteral("enemyIds")));
        for (int i = 0; i < legacyIds.size(); ++i) {
            TroopEnemySlot slot;
            slot.enemyId = legacyIds[i];
            slot.x = legacyIds.size() <= 1 ? 0.5 : qreal(i + 1) / qreal(legacyIds.size() + 1);
            slot.y = 0.48;
            enemySlots.push_back(slot);
        }
    }

    for (const TroopEnemySlot& slot : enemySlots) {
        const core::DatabaseRecord* record = databaseRecord(m_ed, QStringLiteral("enemies"), slot.enemyId);
        if (!record) continue;
        Enemy e;
        e.recordId = record->id;
        e.name = databaseRecordName(m_ed, QStringLiteral("enemies"), record->id, bt("Inimigo"));
        e.arenaX = slot.x;
        e.arenaY = slot.y;
        e.graphicPath = record->data.value(QStringLiteral("graphicPath")).toString();
        if (!e.graphicPath.isEmpty()) {
            e.image = m_ed.preloadedRuntimeImage(e.graphicPath);
            if (e.image.isNull())
                e.image.load(QFileInfo(e.graphicPath).isAbsolute() ? e.graphicPath : QDir(m_ed.projectRoot()).filePath(e.graphicPath));
        }
        e.maxHp = qMax(1, record->data.value(QStringLiteral("hp"), 100).toInt()); e.hp = e.maxHp;
        e.maxMp = qMax(0, record->data.value(QStringLiteral("mp"), 0).toInt()); e.mp = e.maxMp;
        e.attack = qMax(1, record->data.value(QStringLiteral("attack"), 10).toInt());
        e.defense = qMax(0, record->data.value(QStringLiteral("defense"), 5).toInt());
        e.agility = qMax(1, record->data.value(QStringLiteral("agility"), 8).toInt());
        e.hitRate = qBound(1, record->data.value(QStringLiteral("hitRate"), 95).toInt(), 100);
        e.evasion = qBound(0, record->data.value(QStringLiteral("evasion"), 5).toInt(), 95);
        e.critical = qBound(0, record->data.value(QStringLiteral("critical"), 5).toInt(), 100);
        e.skillIds = idList(record->data.value(QStringLiteral("skillIds")));
        e.experience = qMax(0, record->data.value(QStringLiteral("experience"), 0).toInt());
        e.gold = qMax(0, record->data.value(QStringLiteral("gold"), 0).toInt());
        e.lootId = record->data.value(QStringLiteral("lootId")).toString();
        e.lootChance = qBound(0, record->data.value(QStringLiteral("lootChance"), 0).toInt(), 100);
        m_enemies.push_back(e);
    }
    if (m_enemies.isEmpty()) {
        if (error) *error = bt("A tropa não possui inimigos válidos.");
        clear(); return false;
    }
    if (m_state.party().isEmpty()) {
        if (error) *error = bt("O grupo do jogador está vazio.");
        clear(); return false;
    }
    bool living = false;
    for (const PartyMemberState& actor : m_state.party()) if (actor.hp > 0) { living = true; break; }
    if (!living) {
        if (error) *error = bt("Todos os personagens do grupo estão derrotados.");
        clear(); return false;
    }
    if (const core::MapDoc* map = m_ed.doc()) {
        const QString path = map->environment.battleBackgroundPath;
        if (!path.isEmpty()) {
            m_background = m_ed.preloadedRuntimeImage(path);
            if (m_background.isNull())
                m_background.load(QFileInfo(path).isAbsolute() ? path : QDir(m_ed.projectRoot()).filePath(path));
        }
    }
    m_actorTurn = 0;
    while (m_actorTurn < m_state.party().size() && m_state.party()[m_actorTurn].hp <= 0) ++m_actorTurn;
    m_active = true; m_finished = false; m_result = BattleResult::Aborted; m_mode = UiBattleMode::Commands;
    appendLog(bt("%1 apareceu!").arg(m_troopName));
    processBattleEvents(QStringLiteral("battleStart"));
    processBattleEvents(QStringLiteral("roundStart"));
    if (allEnemiesDefeated()) { finishVictory(); return true; }
    if (allPartyDefeated()) { finishDefeat(); return true; }
    rebuildEntries();
    return true;
}

void UiBattleController::clear()
{
    m_enemies.clear(); m_entries.clear(); m_log.clear(); m_background = QImage();
    m_active = false; m_finished = false; m_allowEscape = true; m_result = BattleResult::Aborted;
    m_mode = UiBattleMode::Commands; m_pending = PendingKind::None; m_pendingId.clear();
    m_selected = 0; m_actorTurn = 0; m_round = 1; m_defending.clear();
    m_troopId.clear(); m_troopName.clear(); m_resultSummary.clear();
    m_animation = BattleAnimationDefinition{}; m_animationActive = false; m_animationHitResolved = false;
    m_animationElapsedMs = 0; m_animationTargetsParty = false; m_animationTargets.clear();
    m_pendingResolution = {}; m_soundQueue.clear(); m_animationImageCache.clear();
    m_enemyTurnInProgress = false; m_enemyTurnCursor = 0;
    m_battleEvents.clear(); m_firedBattleEvents.clear(); m_eventLastRound.clear();
}

void UiBattleController::update(double dt)
{
    if (!m_animationActive) return;
    const int previous = m_animationElapsedMs;
    m_animationElapsedMs = qMin(m_animation.durationMs, m_animationElapsedMs + qMax(0, qRound(dt * 1000.0)));
    queueAnimationSounds(previous, m_animationElapsedMs);
    if (!m_animationHitResolved && m_animationElapsedMs >= m_animation.hitFrameMs) resolvePendingHit();
    if (m_animationElapsedMs >= m_animation.durationMs) finishAnimatedResolution();
}

BattleResult UiBattleController::takeResult()
{
    const BattleResult result = m_result;
    m_finished = false; m_result = BattleResult::Aborted; m_resultSummary.clear();
    return result;
}

QString UiBattleController::title() const { return m_mode == UiBattleMode::Result ? battleTr(m_ed,QStringLiteral("system.battle.result"),"Resultado") : battleTr(m_ed,QStringLiteral("system.battle.round"),"Batalha — Rodada %1").arg(m_round); }
QString UiBattleController::actorName() const { const PartyMemberState* actor=currentActor(); return actor?databaseRecordName(m_ed,QStringLiteral("actors"),actor->actorId,bt("Personagem")):QString(); }
QString UiBattleController::prompt() const
{
    if (m_animationActive) return battleTr(m_ed,QStringLiteral("system.battle.executing"),"Executando ação…");
    switch (m_mode) {
    case UiBattleMode::Commands: return battleTr(m_ed,QStringLiteral("system.battle.turn"),"Turno de %1").arg(actorName());
    case UiBattleMode::EnemyTarget: return battleTr(m_ed,QStringLiteral("system.battle.choose_enemy"),"Escolha o inimigo");
    case UiBattleMode::SkillList: return battleTr(m_ed,QStringLiteral("system.battle.choose_skill"),"Escolha uma habilidade");
    case UiBattleMode::ItemList: return battleTr(m_ed,QStringLiteral("system.battle.choose_item"),"Escolha um item");
    case UiBattleMode::AllyTarget: return battleTr(m_ed,QStringLiteral("system.battle.choose_ally"),"Escolha um aliado");
    case UiBattleMode::Result: return m_resultSummary;
    }
    return QString();
}

void UiBattleController::appendLog(const QString& text) { if(text.isEmpty())return; m_log.push_back(text); while(m_log.size()>8)m_log.removeFirst(); }
PartyMemberState* UiBattleController::currentActor() { return m_actorTurn>=0&&m_actorTurn<m_state.party().size()?&m_state.party()[m_actorTurn]:nullptr; }
const PartyMemberState* UiBattleController::currentActor() const { return m_actorTurn>=0&&m_actorTurn<m_state.party().size()?&m_state.party()[m_actorTurn]:nullptr; }
int UiBattleController::firstLivingEnemy() const { for(int i=0;i<m_enemies.size();++i)if(m_enemies[i].hp>0)return i;return -1; }
UiBattleController::Enemy* UiBattleController::selectedLivingEnemy(){if(m_selected>=0&&m_selected<m_enemies.size()&&m_enemies[m_selected].hp>0)return &m_enemies[m_selected];const int i=firstLivingEnemy();if(i>=0){m_selected=i;return &m_enemies[i];}return nullptr;}
bool UiBattleController::allEnemiesDefeated() const { for(const Enemy&e:m_enemies)if(e.hp>0)return false;return true; }
bool UiBattleController::allPartyDefeated() const { for(const PartyMemberState&a:m_state.party())if(a.hp>0)return false;return true; }

QVector<const core::DatabaseRecord*> UiBattleController::actorSkills() const
{
    QVector<const core::DatabaseRecord*> out; const PartyMemberState* actor=currentActor(); if(!actor)return out; QStringList ids;
    if(const auto*r=databaseRecord(m_ed,QStringLiteral("actors"),actor->actorId)){ids+=idList(r->data.value(QStringLiteral("skillIds")));if(const auto*k=databaseRecord(m_ed,QStringLiteral("classes"),r->data.value(QStringLiteral("classId")).toString()))ids+=idList(k->data.value(QStringLiteral("skillIds")));}
    ids.removeDuplicates(); for(const QString&id:ids)if(const auto*s=databaseRecord(m_ed,QStringLiteral("skills"),id))out.push_back(s); return out;
}

QVector<const core::DatabaseRecord*> UiBattleController::usableItems() const
{
    QVector<const core::DatabaseRecord*> out; const auto it=m_ed.database.constFind(QStringLiteral("items")); if(it==m_ed.database.cend())return out;
    for(const auto&i:it.value()){if(m_state.itemCount(i.id)<=0)continue;if(i.data.value(QStringLiteral("healHp")).toInt()==0&&i.data.value(QStringLiteral("healMp")).toInt()==0&&i.data.value(QStringLiteral("stateAddId")).toString().isEmpty()&&i.data.value(QStringLiteral("stateRemoveId")).toString().isEmpty()&&i.data.value(QStringLiteral("power")).toInt()==0)continue;out.push_back(&i);}return out;
}

void UiBattleController::rebuildEntries()
{
    m_entries.clear();
    switch(m_mode){
    case UiBattleMode::Commands:m_entries={battleTr(m_ed,QStringLiteral("system.battle.attack"),"Atacar"),battleTr(m_ed,QStringLiteral("system.battle.skill"),"Habilidade"),battleTr(m_ed,QStringLiteral("system.battle.item"),"Item"),battleTr(m_ed,QStringLiteral("system.battle.defend"),"Defender")};if(m_allowEscape)m_entries.push_back(battleTr(m_ed,QStringLiteral("system.battle.escape"),"Fugir"));break;
    case UiBattleMode::EnemyTarget:for(const Enemy&e:m_enemies)m_entries.push_back(e.hp>0?bt("%1  HP %2/%3").arg(e.name).arg(e.hp).arg(e.maxHp):bt("%1  Derrotado").arg(e.name));break;
    case UiBattleMode::SkillList:for(const auto*s:actorSkills())m_entries.push_back(bt("%1  MP %2").arg(databaseRecordName(m_ed,QStringLiteral("skills"),s->id,s->name)).arg(s->data.value(QStringLiteral("mpCost")).toInt()));break;
    case UiBattleMode::ItemList:for(const auto*i:usableItems())m_entries.push_back(bt("%1  ×%2").arg(databaseRecordName(m_ed,QStringLiteral("items"),i->id,i->name)).arg(m_state.itemCount(i->id)));break;
    case UiBattleMode::AllyTarget:for(const PartyMemberState&a:m_state.party()){const CombatStats st=memberStats(m_ed,a);m_entries.push_back(bt("%1  HP %2/%3  MP %4/%5").arg(databaseRecordName(m_ed,QStringLiteral("actors"),a.actorId,bt("Personagem"))).arg(a.hp).arg(st.maxHp).arg(a.mp).arg(st.maxMp));}break;
    case UiBattleMode::Result:m_entries={battleTr(m_ed,QStringLiteral("system.battle.continue"),"Continuar")};break;
    }
    m_selected=qBound(0,m_selected,qMax(0,m_entries.size()-1));
    if(m_mode==UiBattleMode::EnemyTarget&&m_selected<m_enemies.size()&&m_enemies[m_selected].hp<=0){const int i=firstLivingEnemy();if(i>=0)m_selected=i;}
}

void UiBattleController::selectIndex(int index){if(m_entries.isEmpty())return;m_selected=qBound(0,index,m_entries.size()-1);if(m_mode==UiBattleMode::EnemyTarget&&m_selected<m_enemies.size()&&m_enemies[m_selected].hp<=0){const int f=firstLivingEnemy();if(f>=0)m_selected=f;}}

void UiBattleController::refreshLocalization()
{
    if (!m_active && !m_finished) return;
    if (const auto* troop = databaseRecord(m_ed, QStringLiteral("troops"), m_troopId))
        m_troopName = databaseRecordName(m_ed, QStringLiteral("troops"), troop->id, bt("Batalha"));
    for (Enemy& enemy : m_enemies)
        if (const auto* record = databaseRecord(m_ed, QStringLiteral("enemies"), enemy.recordId))
            enemy.name = databaseRecordName(m_ed, QStringLiteral("enemies"), record->id, bt("Inimigo"));
    rebuildEntries();
}

bool UiBattleController::handleAction(core::GameAction action)
{
    if(!m_active)return false;
    if(m_animationActive||m_enemyTurnInProgress)return true;
    const auto commandMeta=m_ed.gameUi.layoutElements.value(QStringLiteral("battle.commands"));
    const auto commandData=UiDataBindingResolver::resolveRuntime(commandMeta,m_ed,m_state);
    const bool commandEnabled=(!commandData.hasEnabled||commandData.enabled)&&initialScreenStateEnabled(m_ed,QStringLiteral("battle"),QStringLiteral("battle.commands"));
    if(!commandEnabled&&action!=core::GameAction::Cancel&&action!=core::GameAction::Quit)return true;
    if(action==core::GameAction::Up||action==core::GameAction::Left){if(!m_entries.isEmpty()){m_selected=(m_selected-1+m_entries.size())%m_entries.size();if(m_mode==UiBattleMode::EnemyTarget&&m_enemies[m_selected].hp<=0){for(int n=0;n<m_enemies.size();++n){m_selected=(m_selected-1+m_enemies.size())%m_enemies.size();if(m_enemies[m_selected].hp>0)break;}}}return true;}
    if(action==core::GameAction::Down||action==core::GameAction::Right){if(!m_entries.isEmpty()){m_selected=(m_selected+1)%m_entries.size();if(m_mode==UiBattleMode::EnemyTarget&&m_enemies[m_selected].hp<=0){for(int n=0;n<m_enemies.size();++n){m_selected=(m_selected+1)%m_enemies.size();if(m_enemies[m_selected].hp>0)break;}}}return true;}
    if(action==core::GameAction::Cancel||action==core::GameAction::Quit){back();return true;}
    if(action==core::GameAction::Confirm){activateSelected();return true;}
    return false;
}

void UiBattleController::back(){if(m_mode==UiBattleMode::Commands){if(m_allowEscape)tryEscape();return;}if(m_mode==UiBattleMode::Result){m_active=false;return;}m_mode=UiBattleMode::Commands;m_pending=PendingKind::None;m_pendingId.clear();m_selected=0;rebuildEntries();}
void UiBattleController::activateSelected(){if(m_mode==UiBattleMode::Result){m_active=false;return;}if(m_mode==UiBattleMode::Commands){switch(m_selected){case 0:m_pending=PendingKind::Attack;m_mode=UiBattleMode::EnemyTarget;m_selected=qMax(0,firstLivingEnemy());break;case 1:m_mode=UiBattleMode::SkillList;m_selected=0;break;case 2:m_mode=UiBattleMode::ItemList;m_selected=0;break;case 3:defend();return;default:tryEscape();return;}rebuildEntries();return;}if(m_mode==UiBattleMode::EnemyTarget){if(m_pending==PendingKind::Attack)attackSelected();else if(m_pending==PendingKind::Skill){const auto*s=databaseRecord(m_ed,QStringLiteral("skills"),m_pendingId);if(s)applySkill(*s,m_selected);}return;}if(m_mode==UiBattleMode::SkillList){chooseSkill();return;}if(m_mode==UiBattleMode::ItemList){chooseItem();return;}if(m_mode==UiBattleMode::AllyTarget){if(m_pending==PendingKind::Skill){if(const auto*s=databaseRecord(m_ed,QStringLiteral("skills"),m_pendingId))applySkill(*s,m_selected);}else if(m_pending==PendingKind::Item){if(const auto*i=databaseRecord(m_ed,QStringLiteral("items"),m_pendingId))applyItem(*i,m_selected);}return;}}

QString UiBattleController::playerAttackAnimationId() const
{
    const PartyMemberState* actor=currentActor(); if(!actor)return {};
    if(!actor->weaponId.isEmpty()) if(const auto*w=databaseRecord(m_ed,QStringLiteral("weapons"),actor->weaponId)){const QString id=w->data.value(QStringLiteral("animationId")).toString();if(!id.isEmpty())return id;}
    if(const auto*a=databaseRecord(m_ed,QStringLiteral("actors"),actor->actorId)) return a->data.value(QStringLiteral("animationId")).toString();
    return {};
}

QString UiBattleController::enemyAttackAnimationId(int enemyIndex) const
{
    if(enemyIndex<0||enemyIndex>=m_enemies.size())return {};
    if(const auto*enemy=databaseRecord(m_ed,QStringLiteral("enemies"),m_enemies[enemyIndex].recordId))return enemy->data.value(QStringLiteral("animationId")).toString();
    return {};
}

QVector<int> UiBattleController::animationTargetsForScope(const QString& scope, int targetIndex, bool playerSource) const
{
    QVector<int> targets;
    if(playerSource){
        if(scope==QLatin1String("enemyAll")){for(int i=0;i<m_enemies.size();++i)if(m_enemies[i].hp>0)targets.push_back(i);}
        else if(scope==QLatin1String("allyAll")){for(int i=0;i<m_state.party().size();++i)targets.push_back(i);}
        else if(scope==QLatin1String("self"))targets.push_back(m_actorTurn);
        else if(targetIndex>=0)targets.push_back(targetIndex);
    }else{
        if(scope==QLatin1String("enemyAll")){for(int i=0;i<m_state.party().size();++i)if(m_state.party()[i].hp>0)targets.push_back(i);}
        else if(scope==QLatin1String("allyAll")){for(int i=0;i<m_enemies.size();++i)if(m_enemies[i].hp>0)targets.push_back(i);}
        else if(scope==QLatin1String("self"))targets.push_back(m_pendingResolution.sourceIndex);
        else if(targetIndex>=0)targets.push_back(targetIndex);
    }
    return targets;
}

void UiBattleController::attackSelected()
{
    if(!currentActor()||!selectedLivingEnemy())return;
    PendingResolution pending; pending.kind=ResolutionKind::PlayerAttack; pending.targetIndex=m_selected;
    beginAnimatedResolution(playerAttackAnimationId(),false,{m_selected},pending);
}

void UiBattleController::chooseSkill(){const auto skills=actorSkills();if(m_selected<0||m_selected>=skills.size())return;const auto*s=skills[m_selected];PartyMemberState*a=currentActor();if(!a)return;const int cost=qMax(0,s->data.value(QStringLiteral("mpCost")).toInt());if(a->mp<cost){appendLog(bt("MP insuficiente."));return;}const QString scope=s->data.value(QStringLiteral("scope"),QStringLiteral("enemyOne")).toString();m_pending=PendingKind::Skill;m_pendingId=s->id;if(scope==QLatin1String("enemyOne")){m_mode=UiBattleMode::EnemyTarget;m_selected=qMax(0,firstLivingEnemy());rebuildEntries();return;}if(scope==QLatin1String("allyOne")){m_mode=UiBattleMode::AllyTarget;m_selected=qBound(0,m_actorTurn,qMax(0,m_state.party().size()-1));rebuildEntries();return;}applySkill(*s,-1);}
void UiBattleController::chooseItem(){const auto items=usableItems();if(m_selected<0||m_selected>=items.size())return;const auto*i=items[m_selected];m_pending=PendingKind::Item;m_pendingId=i->id;const QString scope=i->data.value(QStringLiteral("scope"),QStringLiteral("allyOne")).toString();if(scope==QLatin1String("allyOne")){m_mode=UiBattleMode::AllyTarget;m_selected=qBound(0,m_actorTurn,qMax(0,m_state.party().size()-1));rebuildEntries();return;}applyItem(*i,-1);}

void UiBattleController::applySkill(const core::DatabaseRecord& skill,int targetIndex)
{
    const QString scope=skill.data.value(QStringLiteral("scope"),QStringLiteral("enemyOne")).toString();
    PendingResolution pending;pending.kind=ResolutionKind::PlayerSkill;pending.targetIndex=targetIndex;pending.recordId=skill.id;
    const bool partyTarget=scope==QLatin1String("self")||scope.startsWith(QLatin1String("ally"));
    beginAnimatedResolution(skill.data.value(QStringLiteral("animationId")).toString(),partyTarget,animationTargetsForScope(scope,targetIndex,true),pending);
}

void UiBattleController::applyItem(const core::DatabaseRecord& item,int targetIndex)
{
    const QString scope=item.data.value(QStringLiteral("scope"),QStringLiteral("allyOne")).toString();
    PendingResolution pending;pending.kind=ResolutionKind::PlayerItem;pending.targetIndex=targetIndex;pending.recordId=item.id;
    const bool partyTarget=scope==QLatin1String("self")||scope.startsWith(QLatin1String("ally"));
    beginAnimatedResolution(item.data.value(QStringLiteral("animationId")).toString(),partyTarget,animationTargetsForScope(scope,targetIndex,true),pending);
}

void UiBattleController::resolvePlayerAttack(int targetIndex)
{
    PartyMemberState* actor=currentActor(); if(!actor||targetIndex<0||targetIndex>=m_enemies.size())return; Enemy& enemy=m_enemies[targetIndex]; if(enemy.hp<=0)return;
    const CombatStats stats=memberStats(m_ed,*actor);const auto*ar=databaseRecord(m_ed,QStringLiteral("actors"),actor->actorId);const int hit=qBound(1,ar?ar->data.value(QStringLiteral("hitRate"),95).toInt():95,100);
    if(QRandomGenerator::global()->bounded(100)>=qBound(5,hit-enemy.evasion,100)){appendLog(bt("%1 errou o ataque em %2.").arg(actorName(),enemy.name));return;}
    int damage=qMax(1,stats.attack-enemy.defense/2+QRandomGenerator::global()->bounded(5)-2);const int crit=qBound(0,ar?ar->data.value(QStringLiteral("critical"),5).toInt():5,100);const bool critical=QRandomGenerator::global()->bounded(100)<crit;if(critical)damage*=2;enemy.hp=qMax(0,enemy.hp-damage);appendLog(bt("%1 atacou %2: %3 de dano.%4").arg(actorName(),enemy.name).arg(damage).arg(critical?bt(" Crítico!"):QString()));
}

void UiBattleController::resolvePlayerSkill(const core::DatabaseRecord& skill,int targetIndex)
{
    PartyMemberState* actor=currentActor();if(!actor)return;const int cost=qMax(0,skill.data.value(QStringLiteral("mpCost")).toInt());if(actor->mp<cost){appendLog(bt("MP insuficiente."));return;}actor->mp-=cost;
    const QString scope=skill.data.value(QStringLiteral("scope"),QStringLiteral("enemyOne")).toString();const int healHp=skill.data.value(QStringLiteral("healHp")).toInt(),healMp=skill.data.value(QStringLiteral("healMp")).toInt();const QString add=skill.data.value(QStringLiteral("stateAddId")).toString(),remove=skill.data.value(QStringLiteral("stateRemoveId")).toString();
    if(scope==QLatin1String("self")||scope==QLatin1String("allyOne")||scope==QLatin1String("allyAll")){
        QVector<PartyMemberState*> targets;if(scope==QLatin1String("self"))targets.push_back(actor);else if(scope==QLatin1String("allyAll")){for(auto&x:m_state.party())targets.push_back(&x);}else if(targetIndex>=0&&targetIndex<m_state.party().size())targets.push_back(&m_state.party()[targetIndex]);
        for(auto*target:targets){const CombatStats st=memberStats(m_ed,*target);target->hp=qBound(0,target->hp+healHp,st.maxHp);target->mp=qBound(0,target->mp+healMp,st.maxMp);addState(*target,add,skill.data.value(QStringLiteral("stateChance"),100).toInt());if(!remove.isEmpty()){target->states.removeAll(remove);target->stateTurns.remove(remove);}}
        appendLog(bt("%1 usou %2 no grupo.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("skills"),skill.id,skill.name)));return;
    }
    const CombatStats st=memberStats(m_ed,*actor);
    auto damage=[&](Enemy& enemy){const int hit=qBound(1,skill.data.value(QStringLiteral("hitRate"),100).toInt(),100);if(QRandomGenerator::global()->bounded(100)>=qBound(5,hit-enemy.evasion,100))return -1;int amount=qMax(1,st.attack/2+skill.data.value(QStringLiteral("power"),10).toInt()-enemy.defense/3);const QString element=skill.data.value(QStringLiteral("element")).toString();if(!element.isEmpty())if(const auto*er=databaseRecord(m_ed,QStringLiteral("enemies"),enemy.recordId))amount=qMax(0,qRound(amount*qBound(0.0,er->data.value(QStringLiteral("elementRates")).toMap().value(element,1.0).toDouble(),10.0)));if(QRandomGenerator::global()->bounded(100)<qBound(0,skill.data.value(QStringLiteral("critical"),0).toInt(),100))amount*=2;enemy.hp=qMax(0,enemy.hp-amount);addState(enemy,add,skill.data.value(QStringLiteral("stateChance"),100).toInt());return amount;};
    if(scope==QLatin1String("enemyAll")){int total=0,hits=0;for(auto&enemy:m_enemies)if(enemy.hp>0){const int d=damage(enemy);if(d>=0){total+=d;++hits;}}appendLog(bt("%1 usou %2: %3 acerto(s), %4 de dano.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("skills"),skill.id,skill.name)).arg(hits).arg(total));}
    else{m_selected=qBound(0,targetIndex,qMax(0,m_enemies.size()-1));Enemy*enemy=selectedLivingEnemy();if(!enemy)return;const int d=damage(*enemy);appendLog(d<0?bt("%1 usou %2, mas errou %3.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("skills"),skill.id,skill.name),enemy->name):bt("%1 usou %2 em %3: %4 de dano.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("skills"),skill.id,skill.name),enemy->name).arg(d));}
}

void UiBattleController::resolvePlayerItem(const core::DatabaseRecord& item,int targetIndex)
{
    if(m_state.itemCount(item.id)<=0)return;
    const QString scope=item.data.value(QStringLiteral("scope"),QStringLiteral("allyOne")).toString();
    if(scope.startsWith(QLatin1String("enemy"))){
        auto damage=[&](Enemy& enemy){const int amount=qMax(0,item.data.value(QStringLiteral("power"),0).toInt());enemy.hp=qMax(0,enemy.hp-amount);addState(enemy,item.data.value(QStringLiteral("stateAddId")).toString(),item.data.value(QStringLiteral("stateChance"),100).toInt());return amount;};
        if(scope==QLatin1String("enemyAll")){int total=0;for(auto&enemy:m_enemies)if(enemy.hp>0)total+=damage(enemy);appendLog(bt("%1 usou %2: %3 de dano total.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("items"),item.id,item.name)).arg(total));}
        else if(targetIndex>=0&&targetIndex<m_enemies.size()){const int d=damage(m_enemies[targetIndex]);appendLog(bt("%1 usou %2 em %3: %4 de dano.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("items"),item.id,item.name),m_enemies[targetIndex].name).arg(d));}
    }else{
        QVector<PartyMemberState*> targets;if(scope==QLatin1String("allyAll")){for(auto&actor:m_state.party())targets.push_back(&actor);}else if(scope==QLatin1String("self")&&currentActor())targets.push_back(currentActor());else if(targetIndex>=0&&targetIndex<m_state.party().size())targets.push_back(&m_state.party()[targetIndex]);else if(currentActor())targets.push_back(currentActor());
        for(auto*target:targets){const CombatStats st=memberStats(m_ed,*target);target->hp=qBound(0,target->hp+item.data.value(QStringLiteral("healHp")).toInt(),st.maxHp);target->mp=qBound(0,target->mp+item.data.value(QStringLiteral("healMp")).toInt(),st.maxMp);addState(*target,item.data.value(QStringLiteral("stateAddId")).toString(),item.data.value(QStringLiteral("stateChance"),100).toInt());const QString remove=item.data.value(QStringLiteral("stateRemoveId")).toString();if(!remove.isEmpty()){target->states.removeAll(remove);target->stateTurns.remove(remove);}}
        appendLog(bt("%1 usou %2.").arg(actorName(),databaseRecordName(m_ed,QStringLiteral("items"),item.id,item.name)));
    }
    if(item.data.value(QStringLiteral("consumable"),true).toBool())m_state.addItem(item.id,-1);
}

void UiBattleController::defend(){if(auto*actor=currentActor()){m_defending.insert(actor->actorId);appendLog(bt("%1 está defendendo.").arg(actorName()));finishPlayerAction();}}
void UiBattleController::tryEscape(){if(!m_allowEscape)return;if(QRandomGenerator::global()->bounded(100)<65){showResult(BattleResult::Escaped,bt("O grupo escapou da batalha."));return;}appendLog(bt("A fuga falhou!"));startEnemyTurn();}

void UiBattleController::finishPlayerAction()
{
    processBattleEvents(QStringLiteral("stateChanged"));
    if(allEnemiesDefeated()){finishVictory();return;}
    if(allPartyDefeated()){finishDefeat();return;}
    int next=m_actorTurn+1;while(next<m_state.party().size()&&m_state.party()[next].hp<=0)++next;
    if(next<m_state.party().size()){m_actorTurn=next;m_mode=UiBattleMode::Commands;m_pending=PendingKind::None;m_pendingId.clear();m_selected=0;rebuildEntries();return;}
    startEnemyTurn();
}

void UiBattleController::startEnemyTurn(){m_enemyTurnInProgress=true;m_enemyTurnCursor=0;continueEnemyTurn();}

const core::DatabaseRecord* UiBattleController::chooseEnemySkill(int enemyIndex) const
{
    if(enemyIndex<0||enemyIndex>=m_enemies.size())return nullptr;const Enemy& enemy=m_enemies[enemyIndex];const auto* enemyRecord=databaseRecord(m_ed,QStringLiteral("enemies"),enemy.recordId);if(!enemyRecord)return nullptr;
    QSet<QString> states;for(auto it=enemy.stateTurns.cbegin();it!=enemy.stateTurns.cend();++it)states.insert(it.key());
    QVector<BattleAiRule> matches=matchingBattleAiRules(battleAiRules(enemyRecord),enemy.hp,enemy.maxHp,enemy.mp,m_round,states);
    QVector<QPair<const core::DatabaseRecord*,int>> weighted;
    for(const BattleAiRule& rule:matches)if(const auto*skill=databaseRecord(m_ed,QStringLiteral("skills"),rule.skillId))if(enemy.mp>=qMax(rule.mpMin,skill->data.value(QStringLiteral("mpCost")).toInt()))weighted.push_back({skill,qMax(1,rule.weight)});
    if(weighted.isEmpty()&&battleAiRules(enemyRecord).isEmpty()){
        for(const QString&id:enemy.skillIds)if(const auto*skill=databaseRecord(m_ed,QStringLiteral("skills"),id))if(enemy.mp>=qMax(0,skill->data.value(QStringLiteral("mpCost")).toInt())&&QRandomGenerator::global()->bounded(100)<qBound(0,skill->data.value(QStringLiteral("useChance"),100).toInt(),100))weighted.push_back({skill,10});
    }
    int total=0;for(const auto& entry:weighted)total+=entry.second;if(total<=0)return nullptr;int roll=QRandomGenerator::global()->bounded(total);for(const auto&entry:weighted){if(roll<entry.second)return entry.first;roll-=entry.second;}return weighted.last().first;
}

void UiBattleController::continueEnemyTurn()
{
    if(!m_enemyTurnInProgress||m_animationActive)return;
    if(allEnemiesDefeated()){m_enemyTurnInProgress=false;finishVictory();return;}
    if(allPartyDefeated()){m_enemyTurnInProgress=false;finishDefeat();return;}
    while(m_enemyTurnCursor<m_enemies.size()&&m_enemies[m_enemyTurnCursor].hp<=0)++m_enemyTurnCursor;
    if(m_enemyTurnCursor>=m_enemies.size()){
        m_enemyTurnInProgress=false;m_defending.clear();processBattleEvents(QStringLiteral("roundEnd"));finishRoundStates();processBattleEvents(QStringLiteral("stateChanged"));
        if(allEnemiesDefeated()){finishVictory();return;}if(allPartyDefeated()){finishDefeat();return;}
        m_actorTurn=0;while(m_actorTurn<m_state.party().size()&&m_state.party()[m_actorTurn].hp<=0)++m_actorTurn;m_mode=UiBattleMode::Commands;m_pending=PendingKind::None;m_pendingId.clear();m_selected=0;processBattleEvents(QStringLiteral("roundStart"));if(allEnemiesDefeated()){finishVictory();return;}if(allPartyDefeated()){finishDefeat();return;}rebuildEntries();return;
    }
    const int enemyIndex=m_enemyTurnCursor;const core::DatabaseRecord* skill=chooseEnemySkill(enemyIndex);const QString scope=skill?skill->data.value(QStringLiteral("scope"),QStringLiteral("enemyOne")).toString():QStringLiteral("enemyOne");
    int targetIndex=-1;bool targetsParty=true;
    if(skill&&(scope==QLatin1String("self")||scope.startsWith(QLatin1String("ally")))){targetIndex=enemyIndex;targetsParty=false;}
    else{const QVector<int> alive=livingPartyIndices(m_state);if(alive.isEmpty()){finishDefeat();return;}targetIndex=alive[QRandomGenerator::global()->bounded(int(alive.size()))];targetsParty=true;}
    PendingResolution pending;pending.kind=skill?ResolutionKind::EnemySkill:ResolutionKind::EnemyAttack;pending.sourceIndex=enemyIndex;pending.targetIndex=targetIndex;pending.recordId=skill?skill->id:QString();m_pendingResolution=pending;
    QVector<int> targets;if(skill)targets=animationTargetsForScope(scope,targetIndex,false);else targets={targetIndex};
    const QString animationId=skill?skill->data.value(QStringLiteral("animationId")).toString():enemyAttackAnimationId(enemyIndex);
    beginAnimatedResolution(animationId,targetsParty,targets,pending);
}

void UiBattleController::resolveEnemyAction(int enemyIndex,const core::DatabaseRecord* skill,int targetIndex)
{
    if(enemyIndex<0||enemyIndex>=m_enemies.size())return;Enemy& enemy=m_enemies[enemyIndex];if(enemy.hp<=0)return;
    const QString scope=skill?skill->data.value(QStringLiteral("scope"),QStringLiteral("enemyOne")).toString():QStringLiteral("enemyOne");
    if(skill&&(scope==QLatin1String("self")||scope==QLatin1String("allyOne")||scope==QLatin1String("allyAll"))){
        enemy.mp-=qMax(0,skill->data.value(QStringLiteral("mpCost")).toInt());QVector<Enemy*> recipients;if(scope==QLatin1String("allyAll")){for(Enemy& ally:m_enemies)if(ally.hp>0)recipients.push_back(&ally);}else recipients.push_back(&enemy);
        for(Enemy* ally:recipients){ally->hp=qBound(0,ally->hp+skill->data.value(QStringLiteral("healHp")).toInt(),ally->maxHp);ally->mp=qBound(0,ally->mp+skill->data.value(QStringLiteral("healMp")).toInt(),ally->maxMp);addState(*ally,skill->data.value(QStringLiteral("stateAddId")).toString(),skill->data.value(QStringLiteral("stateChance"),100).toInt());const QString remove=skill->data.value(QStringLiteral("stateRemoveId")).toString();if(!remove.isEmpty())ally->stateTurns.remove(remove);}
        appendLog(bt("%1 usou %2 no próprio grupo.").arg(enemy.name,databaseRecordName(m_ed,QStringLiteral("skills"),skill->id,skill->name)));return;
    }
    QVector<int> targets;if(skill&&scope==QLatin1String("enemyAll"))targets=livingPartyIndices(m_state);else if(targetIndex>=0)targets={targetIndex};
    int totalDamage=0,hits=0;for(const int index:targets){if(index<0||index>=m_state.party().size()||m_state.party()[index].hp<=0)continue;PartyMemberState& target=m_state.party()[index];const CombatStats stats=memberStats(m_ed,target);const int hitRate=skill?qBound(1,skill->data.value(QStringLiteral("hitRate"),enemy.hitRate).toInt(),100):enemy.hitRate;const auto*actorRecord=databaseRecord(m_ed,QStringLiteral("actors"),target.actorId);const int evasion=qBound(0,actorRecord?actorRecord->data.value(QStringLiteral("evasion"),5).toInt():5,95);if(QRandomGenerator::global()->bounded(100)>=qBound(5,hitRate-evasion,100)){appendLog(bt("%1 errou %2.").arg(enemy.name,databaseRecordName(m_ed,QStringLiteral("actors"),target.actorId,bt("Personagem"))));continue;}if(skill&&hits==0)enemy.mp-=qMax(0,skill->data.value(QStringLiteral("mpCost")).toInt());int damage=qMax(1,enemy.attack-stats.defense/2+QRandomGenerator::global()->bounded(5)-2);if(skill)damage=qMax(1,enemy.attack/2+skill->data.value(QStringLiteral("power"),10).toInt()-stats.defense/3);if(skill){const QString element=skill->data.value(QStringLiteral("element")).toString();if(!element.isEmpty()&&actorRecord)damage=qMax(0,qRound(damage*qBound(0.0,actorRecord->data.value(QStringLiteral("elementRates")).toMap().value(element,1.0).toDouble(),10.0)));}const int criticalChance=skill?qBound(0,skill->data.value(QStringLiteral("critical"),enemy.critical).toInt(),100):enemy.critical;const bool critical=QRandomGenerator::global()->bounded(100)<criticalChance;if(critical)damage*=2;if(m_defending.contains(target.actorId))damage=qMax(1,damage/2);target.hp=qMax(0,target.hp-damage);if(skill)addState(target,skill->data.value(QStringLiteral("stateAddId")).toString(),skill->data.value(QStringLiteral("stateChance"),100).toInt());totalDamage+=damage;++hits;appendLog(bt("%1 usou %2 em %3: %4 de dano.%5").arg(enemy.name,skill?databaseRecordName(m_ed,QStringLiteral("skills"),skill->id,skill->name):battleTr(m_ed,QStringLiteral("system.battle.attack"),"Ataque"),databaseRecordName(m_ed,QStringLiteral("actors"),target.actorId,bt("Personagem"))).arg(damage).arg(critical?bt(" Crítico!"):QString()));}
    Q_UNUSED(totalDamage);Q_UNUSED(hits);
}

void UiBattleController::addState(PartyMemberState&target,const QString&id,int chance){const auto*state=databaseRecord(m_ed,QStringLiteral("states"),id);if(!state||QRandomGenerator::global()->bounded(100)>=qBound(0,chance,100))return;if(!target.states.contains(id))target.states.push_back(id);target.stateTurns[id]=qMax(0,state->data.value(QStringLiteral("duration"),0).toInt());}
void UiBattleController::addState(Enemy&target,const QString&id,int chance){const auto*state=databaseRecord(m_ed,QStringLiteral("states"),id);if(!state||QRandomGenerator::global()->bounded(100)>=qBound(0,chance,100))return;target.stateTurns[id]=qMax(0,state->data.value(QStringLiteral("duration"),0).toInt());}

void UiBattleController::finishRoundStates(){auto process=[this](auto&target,int maxHp,const QString&name){QStringList remove;for(auto it=target.stateTurns.begin();it!=target.stateTurns.end();++it){const auto*state=databaseRecord(m_ed,QStringLiteral("states"),it.key());if(!state){remove.push_back(it.key());continue;}const int rate=qBound(0,state->data.value(QStringLiteral("hpDamageRate"),0).toInt(),100);if(rate>0&&target.hp>0){const int damage=qMax(1,maxHp*rate/100);target.hp=qMax(0,target.hp-damage);appendLog(bt("%1 sofreu %2 de dano por %3.").arg(name).arg(damage).arg(databaseRecordName(m_ed,QStringLiteral("states"),state->id,state->name)));}if(it.value()>0&&--it.value()<=0)remove.push_back(it.key());}for(const QString&id:remove)target.stateTurns.remove(id);};for(auto&actor:m_state.party()){process(actor,memberStats(m_ed,actor).maxHp,databaseRecordName(m_ed,QStringLiteral("actors"),actor.actorId,bt("Personagem")));for(int i=actor.states.size()-1;i>=0;--i)if(!actor.stateTurns.contains(actor.states[i]))actor.states.removeAt(i);}for(auto&enemy:m_enemies)process(enemy,enemy.maxHp,enemy.name);++m_round;}
void UiBattleController::finishVictory(){int exp=0,gold=0;QStringList loot,levels;for(const Enemy&enemy:m_enemies){exp+=enemy.experience;gold+=enemy.gold;if(!enemy.lootId.isEmpty()&&QRandomGenerator::global()->bounded(100)<enemy.lootChance){m_state.addItem(enemy.lootId,1);const core::DatabaseRecord*record=nullptr;for(const QString&cat:{QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")})if((record=databaseRecord(m_ed,cat,enemy.lootId)))break;if(record){QString category;for(const QString&cat:{QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")})if(databaseRecord(m_ed,cat,enemy.lootId)){category=cat;break;}loot.push_back(category.isEmpty()?record->name:databaseRecordName(m_ed,category,record->id,record->name));}else loot.push_back(enemy.lootId);}}m_state.addGold(gold);for(auto&actor:m_state.party())gainExperience(m_ed,actor,exp,&levels);QString summary=bt("Vitória!\n%1 EXP\n%2 G").arg(exp).arg(gold);if(!loot.isEmpty())summary+=bt("\nItens: %1").arg(loot.join(QStringLiteral(", ")));if(!levels.isEmpty())summary+=QLatin1Char('\n')+levels.join(QLatin1Char('\n'));showResult(BattleResult::Victory,summary);}
void UiBattleController::finishDefeat(){showResult(BattleResult::Defeat,bt("O grupo foi derrotado."));}
void UiBattleController::showResult(BattleResult result,const QString&summary){m_animationActive=false;m_enemyTurnInProgress=false;m_result=result;m_resultSummary=summary;m_finished=true;m_mode=UiBattleMode::Result;m_selected=0;rebuildEntries();}

void UiBattleController::beginAnimatedResolution(const QString& animationId,bool targetsParty,const QVector<int>& targets,const PendingResolution& pending)
{
    m_pendingResolution=pending;m_animationTargetsParty=targetsParty;m_animationTargets=targets;m_animationElapsedMs=0;m_animationHitResolved=false;
    const core::DatabaseRecord* record=databaseRecord(m_ed,QStringLiteral("animations"),animationId);
    if(!record){m_animationActive=false;resolvePendingHit();finishAnimatedResolution();return;}
    m_animation=BattleAnimationDefinition::fromRecord(record);m_animation.durationMs=qMax(1,m_animation.durationMs);m_animation.hitFrameMs=qBound(0,m_animation.hitFrameMs,m_animation.durationMs);m_animationActive=true;queueAnimationSounds(-1,0);if(m_animation.hitFrameMs==0)resolvePendingHit();
}

void UiBattleController::resolvePendingHit()
{
    if(m_animationHitResolved)return;m_animationHitResolved=true;
    const PendingResolution pending=m_pendingResolution;
    switch(pending.kind){
    case ResolutionKind::PlayerAttack:resolvePlayerAttack(pending.targetIndex);break;
    case ResolutionKind::PlayerSkill:if(const auto*record=databaseRecord(m_ed,QStringLiteral("skills"),pending.recordId))resolvePlayerSkill(*record,pending.targetIndex);break;
    case ResolutionKind::PlayerItem:if(const auto*record=databaseRecord(m_ed,QStringLiteral("items"),pending.recordId))resolvePlayerItem(*record,pending.targetIndex);break;
    case ResolutionKind::EnemyAttack:resolveEnemyAction(pending.sourceIndex,nullptr,pending.targetIndex);break;
    case ResolutionKind::EnemySkill:if(const auto*record=databaseRecord(m_ed,QStringLiteral("skills"),pending.recordId))resolveEnemyAction(pending.sourceIndex,record,pending.targetIndex);break;
    case ResolutionKind::None:break;
    }
    processBattleEvents(QStringLiteral("stateChanged"));
}

void UiBattleController::finishAnimatedResolution()
{
    if(!m_animationHitResolved)resolvePendingHit();const ResolutionKind kind=m_pendingResolution.kind;m_animationActive=false;m_animationElapsedMs=0;m_animationTargets.clear();m_pendingResolution={};
    if(kind==ResolutionKind::PlayerAttack||kind==ResolutionKind::PlayerSkill||kind==ResolutionKind::PlayerItem){finishPlayerAction();return;}
    if(kind==ResolutionKind::EnemyAttack||kind==ResolutionKind::EnemySkill){++m_enemyTurnCursor;if(allPartyDefeated()){m_enemyTurnInProgress=false;finishDefeat();return;}continueEnemyTurn();}
}

void UiBattleController::queueAnimationSounds(int previousMs,int currentMs)
{
    if(!m_animationActive)return;
    for(const BattleAnimationCue& cue:m_animation.cues){if(cue.type!=QLatin1String("se")||cue.assetPath.isEmpty())continue;if(cue.timeMs>previousMs&&cue.timeMs<=currentMs)m_soundQueue.push_back({cue.assetPath,qBound(0,cue.volume,100)});}
}

QImage UiBattleController::animationImage(const QString& path) const
{
    if(path.isEmpty())return {};auto it=m_animationImageCache.constFind(path);if(it!=m_animationImageCache.cend())return it.value();QImage image=m_ed.preloadedRuntimeImage(path);if(image.isNull())image.load(QFileInfo(path).isAbsolute()?path:QDir(m_ed.projectRoot()).filePath(path));m_animationImageCache.insert(path,image);return image;
}

QVector<BattleSoundCue> UiBattleController::takeSoundEffects(){QVector<BattleSoundCue> out=m_soundQueue;m_soundQueue.clear();return out;}

bool UiBattleController::battleEventConditionMatches(const BattleEventRule& rule,const QString& trigger) const
{
    if(rule.trigger==QLatin1String("battleStart")||rule.trigger==QLatin1String("roundStart")||rule.trigger==QLatin1String("roundEnd")){
        if(rule.trigger!=trigger)return false;if((rule.trigger==QLatin1String("roundStart")||rule.trigger==QLatin1String("roundEnd"))&&rule.value>0&&m_round!=rule.value)return false;return true;
    }
    if(trigger!=QLatin1String("stateChanged"))return false;
    if(rule.trigger==QLatin1String("enemyHpBelow")){for(const Enemy&enemy:m_enemies){if(!rule.enemyId.isEmpty()&&enemy.recordId!=rule.enemyId)continue;const int percent=enemy.maxHp>0?enemy.hp*100/enemy.maxHp:0;if(percent<=qBound(0,rule.value,100))return true;}return false;}
    if(rule.trigger==QLatin1String("partyHpBelow")){for(const PartyMemberState&actor:m_state.party()){const CombatStats stats=memberStats(m_ed,actor);const int percent=stats.maxHp>0?actor.hp*100/stats.maxHp:0;if(percent<=qBound(0,rule.value,100))return true;}return false;}
    return false;
}

void UiBattleController::processBattleEvents(const QString& trigger)
{
    for(const BattleEventRule& rule:m_battleEvents){if(rule.once&&m_firedBattleEvents.contains(rule.id))continue;if(!rule.once&&m_eventLastRound.value(rule.id,-1)==m_round)continue;if(!battleEventConditionMatches(rule,trigger))continue;executeBattleEvent(rule);if(rule.once)m_firedBattleEvents.insert(rule.id);else m_eventLastRound[rule.id]=m_round;}
}

void UiBattleController::executeBattleEvent(const BattleEventRule& rule)
{
    const QVariantMap action=rule.action;const QString type=action.value(QStringLiteral("type"),QStringLiteral("message")).toString();const int amount=action.value(QStringLiteral("amount"),0).toInt();const QString targetId=action.value(QStringLiteral("targetId")).toString();
    if(type==QLatin1String("message")){appendLog(action.value(QStringLiteral("text")).toString());return;}
    if(type==QLatin1String("switch")){m_state.setSwitch(qMax(1,action.value(QStringLiteral("id"),1).toInt()),action.value(QStringLiteral("value"),true).toBool());return;}
    if(type==QLatin1String("variableSet")){m_state.setVariable(qMax(1,action.value(QStringLiteral("id"),1).toInt()),amount);return;}
    if(type==QLatin1String("variableAdd")){const int id=qMax(1,action.value(QStringLiteral("id"),1).toInt());m_state.setVariable(id,m_state.variable(id)+amount);return;}
    if(type==QLatin1String("se")){const QString path=action.value(QStringLiteral("path")).toString();if(!path.isEmpty())m_soundQueue.push_back({path,qBound(0,action.value(QStringLiteral("volume"),90).toInt(),100)});return;}
    if(type==QLatin1String("enemyHeal")||type==QLatin1String("enemyDamage")){for(Enemy&enemy:m_enemies){if(!targetId.isEmpty()&&enemy.recordId!=targetId)continue;enemy.hp=qBound(0,enemy.hp+(type==QLatin1String("enemyHeal")?qAbs(amount):-qAbs(amount)),enemy.maxHp);}return;}
    if(type==QLatin1String("partyHeal")||type==QLatin1String("partyDamage")){for(PartyMemberState&actor:m_state.party()){if(!targetId.isEmpty()&&actor.actorId!=targetId)continue;const CombatStats stats=memberStats(m_ed,actor);actor.hp=qBound(0,actor.hp+(type==QLatin1String("partyHeal")?qAbs(amount):-qAbs(amount)),stats.maxHp);}return;}
    if(type==QLatin1String("enemyState")){for(Enemy&enemy:m_enemies)if(targetId.isEmpty()||enemy.recordId==targetId)addState(enemy,action.value(QStringLiteral("stateId")).toString(),action.value(QStringLiteral("chance"),100).toInt());return;}
    if(type==QLatin1String("partyState")){for(PartyMemberState&actor:m_state.party())if(targetId.isEmpty()||actor.actorId==targetId)addState(actor,action.value(QStringLiteral("stateId")).toString(),action.value(QStringLiteral("chance"),100).toInt());}
}

} // namespace game::ui
