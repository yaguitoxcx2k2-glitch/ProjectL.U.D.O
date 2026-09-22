#include "GameMenuDialog.h"

#include "game/GameSave.h"
#include "game/GameSession.h"
#include "game/GamepadInput.h"
#include "game/RpgSystem.h"
#include "game/SaveLoadDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace game {
namespace {

QString formattedTime(qint64 seconds)
{
    if (seconds < 0) seconds = 0;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

const core::DatabaseRecord* inventoryRecord(const core::Editor& ed, const QString& id,
                                            QString* category = nullptr)
{
    for (const QString& cat : {QStringLiteral("items"), QStringLiteral("weapons"),
                               QStringLiteral("armors")}) {
        if (const core::DatabaseRecord* record = databaseRecord(ed, cat, id)) {
            if (category) *category = cat;
            return record;
        }
    }
    return nullptr;
}

} // namespace

GameMenuDialog::GameMenuDialog(GameSession& session, bool standalone, QWidget* parent)
    : QDialog(parent), m_session(session), m_standalone(standalone)
{
    setWindowTitle(tr("Menu do jogo"));
    resize(820, 620);
    setModal(true);
    auto* root = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("font-weight:600;padding:6px"));
    root->addWidget(m_summary);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    auto* statusPage = new QWidget(tabs);
    auto* statusLayout = new QHBoxLayout(statusPage);
    m_partyList = new QListWidget(statusPage);
    m_partyList->setMinimumWidth(220);
    m_statusDetails = new QLabel(statusPage);
    m_statusDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_statusDetails->setWordWrap(true);
    m_statusDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(m_partyList);
    statusLayout->addWidget(m_statusDetails, 1);
    tabs->addTab(statusPage, tr("Status"));

    auto* itemPage = new QWidget(tabs);
    auto* itemLayout = new QHBoxLayout(itemPage);
    m_inventoryList = new QListWidget(itemPage);
    m_inventoryList->setMinimumWidth(330);
    auto* itemRight = new QWidget(itemPage);
    auto* itemRightLayout = new QVBoxLayout(itemRight);
    m_itemDetails = new QLabel(itemRight);
    m_itemDetails->setWordWrap(true);
    m_itemDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    itemRightLayout->addWidget(m_itemDetails, 1);
    auto* targetForm = new QFormLayout;
    m_itemTarget = new QComboBox(itemRight);
    targetForm->addRow(tr("Alvo:"), m_itemTarget);
    itemRightLayout->addLayout(targetForm);
    m_useItem = new QPushButton(tr("Usar item"), itemRight);
    itemRightLayout->addWidget(m_useItem);
    itemLayout->addWidget(m_inventoryList, 1);
    itemLayout->addWidget(itemRight, 1);
    tabs->addTab(itemPage, tr("Itens"));

    auto* equipPage = new QWidget(tabs);
    auto* equipLayout = new QVBoxLayout(equipPage);
    auto* equipForm = new QFormLayout;
    m_equipActor = new QComboBox(equipPage);
    m_weapon = new QComboBox(equipPage);
    m_armor = new QComboBox(equipPage);
    m_accessory = new QComboBox(equipPage);
    equipForm->addRow(tr("Personagem:"), m_equipActor);
    equipForm->addRow(tr("Arma:"), m_weapon);
    equipForm->addRow(tr("Armadura:"), m_armor);
    equipForm->addRow(tr("Acessório:"), m_accessory);
    equipLayout->addLayout(equipForm);
    m_equipDetails = new QLabel(equipPage);
    m_equipDetails->setWordWrap(true);
    m_equipDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    equipLayout->addWidget(m_equipDetails, 1);
    auto* apply = new QPushButton(tr("Aplicar equipamentos"), equipPage);
    equipLayout->addWidget(apply, 0, Qt::AlignRight);
    tabs->addTab(equipPage, tr("Equipamento"));

    auto* questPage = new QWidget(tabs);
    auto* questLayout = new QHBoxLayout(questPage);
    m_questList = new QListWidget(questPage);
    m_questList->setMinimumWidth(280);
    m_questDetails = new QLabel(questPage);
    m_questDetails->setWordWrap(true);
    m_questDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    questLayout->addWidget(m_questList, 1);
    questLayout->addWidget(m_questDetails, 2);
    tabs->addTab(questPage, tr("Missões"));

    auto* savePage = new QWidget(tabs);
    auto* saveLayout = new QVBoxLayout(savePage);
    saveLayout->addStretch(1);
    auto* save = new QPushButton(tr("Salvar partida…"), savePage);
    auto* load = new QPushButton(tr("Carregar partida…"), savePage);
    save->setMinimumWidth(280);
    load->setMinimumWidth(280);
    saveLayout->addWidget(save, 0, Qt::AlignHCenter);
    saveLayout->addWidget(load, 0, Qt::AlignHCenter);
    saveLayout->addStretch(1);
    tabs->addTab(savePage, tr("Salvar"));

    auto* systemPage = new QWidget(tabs);
    auto* systemLayout = new QVBoxLayout(systemPage);
    auto* systemInfo = new QLabel(tr("Tempo de jogo: %1\nMapa atual: %2")
                                      .arg(formattedTime(m_session.playTimeSeconds()),
                                           m_session.editor().doc() ? m_session.editor().doc()->name : QString()),
                                  systemPage);
    systemInfo->setAlignment(Qt::AlignCenter);
    systemLayout->addStretch(1);
    systemLayout->addWidget(systemInfo);
    auto* returnButton = new QPushButton(m_standalone ? tr("Voltar ao título")
                                                      : tr("Encerrar teste"), systemPage);
    returnButton->setMinimumWidth(280);
    systemLayout->addWidget(returnButton, 0, Qt::AlignHCenter);
    systemLayout->addStretch(1);
    tabs->addTab(systemPage, tr("Sistema"));

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(returnButton, &QPushButton::clicked, this, [this] { done(ReturnToTitle); });
    connect(m_partyList, &QListWidget::currentRowChanged, this,
            [this](int) { refreshStatusDetails(); });
    connect(m_inventoryList, &QListWidget::currentRowChanged, this,
            [this](int) { refreshItemDetails(); });
    connect(m_equipActor, &QComboBox::currentIndexChanged, this,
            [this](int) { rebuildEquipment(); });
    connect(m_weapon, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshEquipmentDetails(); });
    connect(m_armor, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshEquipmentDetails(); });
    connect(m_accessory, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshEquipmentDetails(); });
    connect(m_questList, &QListWidget::currentRowChanged, this,
            [this](int) { refreshQuestDetails(); });
    connect(m_useItem, &QPushButton::clicked, this, &GameMenuDialog::useSelectedItem);
    connect(apply, &QPushButton::clicked, this, &GameMenuDialog::applyEquipment);
    connect(save, &QPushButton::clicked, this, [this] { openSaveLoad(false); });
    connect(load, &QPushButton::clicked, this, [this] { openSaveLoad(true); });
    rebuildAll();
    m_partyList->setFocus();
    new GamepadDialogNavigator(this, m_session.editor().inputSystem);
}

QString GameMenuDialog::memberName(const QString& actorId) const
{
    return databaseRecordName(m_session.editor(), QStringLiteral("actors"), actorId,
                              tr("Personagem"));
}

void GameMenuDialog::rebuildAll()
{
    const QString mapName = m_session.editor().doc() ? m_session.editor().doc()->name : QString();
    m_summary->setText(tr("%1 G   ·   %2   ·   %3")
                           .arg(m_session.state().gold())
                           .arg(formattedTime(m_session.playTimeSeconds()), mapName));
    rebuildParty();
    rebuildInventory();
    rebuildQuests();
}

void GameMenuDialog::rebuildQuests()
{
    const QString selected = m_questList->currentItem()
        ? m_questList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_questList->clear();
    for (const QString& id : m_session.state().questIds()) {
        const QuestState state = m_session.state().quest(id);
        const core::DatabaseRecord* record = databaseRecord(m_session.editor(), QStringLiteral("quests"), id);
        if (!record) continue;
        const QString status = state.status == QLatin1String("completed") ? tr("Concluída")
            : state.status == QLatin1String("failed") ? tr("Falhou") : tr("Ativa");
        auto* item = new QListWidgetItem(tr("%1\n%2 · %3/%4")
                                             .arg(record->name, status)
                                             .arg(state.progress).arg(state.target), m_questList);
        item->setData(Qt::UserRole, id);
    }
    int row = 0;
    for (int index = 0; index < m_questList->count(); ++index)
        if (m_questList->item(index)->data(Qt::UserRole).toString() == selected) row = index;
    if (m_questList->count() > 0) m_questList->setCurrentRow(row);
    refreshQuestDetails();
}

void GameMenuDialog::refreshQuestDetails()
{
    if (!m_questList->currentItem()) {
        m_questDetails->setText(tr("Nenhuma missão foi iniciada."));
        return;
    }
    const QString id = m_questList->currentItem()->data(Qt::UserRole).toString();
    const core::DatabaseRecord* record = databaseRecord(m_session.editor(), QStringLiteral("quests"), id);
    if (!record) { m_questDetails->clear(); return; }
    const QuestState state = m_session.state().quest(id);
    const QString status = state.status == QLatin1String("completed") ? tr("Concluída")
        : state.status == QLatin1String("failed") ? tr("Falhou") : tr("Em andamento");
    m_questDetails->setText(tr("<h2>%1</h2><p>%2</p><p><b>%3</b><br>Progresso: %4 / %5</p>")
                                .arg(record->name, record->description, status)
                                .arg(state.progress).arg(state.target));
}

void GameMenuDialog::rebuildParty()
{
    const QString selected = m_partyList->currentItem()
                                 ? m_partyList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_partyList->clear();
    m_itemTarget->clear();
    const QVector<PartyMemberState>& party = m_session.state().party();
    for (const PartyMemberState& member : party) {
        const CombatStats stats = memberStats(m_session.editor(), member);
        const QString name = memberName(member.actorId);
        auto* item = new QListWidgetItem(tr("%1   Nv %2\nHP %3/%4   MP %5/%6")
                                             .arg(name).arg(member.level).arg(member.hp).arg(stats.maxHp)
                                             .arg(member.mp).arg(stats.maxMp), m_partyList);
        item->setData(Qt::UserRole, member.actorId);
        m_itemTarget->addItem(name, member.actorId);
    }
    int row = 0;
    for (int index = 0; index < m_partyList->count(); ++index)
        if (m_partyList->item(index)->data(Qt::UserRole).toString() == selected) row = index;
    if (!party.isEmpty()) m_partyList->setCurrentRow(row);

    const QVariant selectedActor = m_equipActor->currentData();
    {
        QSignalBlocker blocker(m_equipActor);
        m_equipActor->clear();
        for (const PartyMemberState& member : party)
            m_equipActor->addItem(memberName(member.actorId), member.actorId);
        m_equipActor->setCurrentIndex(qMax(0, m_equipActor->findData(selectedActor)));
    }
    rebuildEquipment();
    refreshStatusDetails();
}

void GameMenuDialog::refreshStatusDetails()
{
    if (!m_partyList->currentItem()) {
        m_statusDetails->setText(tr("O grupo está vazio."));
        return;
    }
    const QString actorId = m_partyList->currentItem()->data(Qt::UserRole).toString();
    const PartyMemberState* member = m_session.state().partyMember(actorId);
    if (!member) return;
    const CombatStats stats = memberStats(m_session.editor(), *member);
    const core::DatabaseRecord* actor = databaseRecord(m_session.editor(), QStringLiteral("actors"), actorId);
    const QString className = actor
        ? databaseRecordName(m_session.editor(), QStringLiteral("classes"),
                             actor->data.value(QStringLiteral("classId")).toString(), tr("Sem classe"))
        : tr("Sem classe");
    const int next = experienceToNextLevel(m_session.editor(), *member);
    const QString statusHtml = tr(
        "<h2>%1</h2><p>%2 · Nível %3</p>"
        "<p><b>HP:</b> %4 / %5<br><b>MP:</b> %6 / %7<br>"
        "<b>Ataque:</b> %8<br><b>Defesa:</b> %9<br><b>Agilidade:</b> %10</p>"
        "<p><b>EXP:</b> %11<br><b>Para o próximo nível:</b> %12</p>"
        "<p><b>Arma:</b> %13<br><b>Armadura:</b> %14<br><b>Acessório:</b> %15</p>")
        .arg(memberName(actorId), className).arg(member->level)
        .arg(member->hp).arg(stats.maxHp).arg(member->mp).arg(stats.maxMp)
        .arg(stats.attack).arg(stats.defense).arg(stats.agility)
        .arg(member->experience).arg(next > 0 ? QString::number(next) : tr("Máximo"),
             databaseRecordName(m_session.editor(), QStringLiteral("weapons"), member->weaponId, tr("Nenhuma")),
             databaseRecordName(m_session.editor(), QStringLiteral("armors"), member->armorId, tr("Nenhuma")),
             databaseRecordName(m_session.editor(), QStringLiteral("armors"), member->accessoryId, tr("Nenhum")));
    m_statusDetails->setText(statusHtml);
}

void GameMenuDialog::rebuildInventory()
{
    const QString selected = m_inventoryList->currentItem()
                                 ? m_inventoryList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_inventoryList->clear();
    for (const QString& id : m_session.state().inventoryIds()) {
        QString category;
        const core::DatabaseRecord* record = inventoryRecord(m_session.editor(), id, &category);
        const QString name = record && !record->name.isEmpty() ? record->name : id;
        auto* item = new QListWidgetItem(tr("%1  ×%2").arg(name).arg(m_session.state().itemCount(id)),
                                         m_inventoryList);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, category);
    }
    int row = 0;
    for (int index = 0; index < m_inventoryList->count(); ++index)
        if (m_inventoryList->item(index)->data(Qt::UserRole).toString() == selected) row = index;
    if (m_inventoryList->count() > 0) m_inventoryList->setCurrentRow(row);
    refreshItemDetails();
}

void GameMenuDialog::refreshItemDetails()
{
    if (!m_inventoryList->currentItem()) {
        m_itemDetails->setText(tr("O inventário está vazio."));
        m_useItem->setEnabled(false);
        return;
    }
    const QString id = m_inventoryList->currentItem()->data(Qt::UserRole).toString();
    QString category;
    const core::DatabaseRecord* record = inventoryRecord(m_session.editor(), id, &category);
    if (!record) {
        m_itemDetails->setText(id);
        m_useItem->setEnabled(false);
        return;
    }
    QString effect;
    if (category == QLatin1String("items")) {
        const int healHp = record->data.value(QStringLiteral("healHp")).toInt();
        const int healMp = record->data.value(QStringLiteral("healMp")).toInt();
        if (healHp) effect += tr("HP %1 ").arg(healHp > 0 ? QStringLiteral("+") + QString::number(healHp)
                                                          : QString::number(healHp));
        if (healMp) effect += tr("MP %1").arg(healMp > 0 ? QStringLiteral("+") + QString::number(healMp)
                                                         : QString::number(healMp));
    }
    m_itemDetails->setText(tr("<h3>%1</h3><p>%2</p><p>%3</p><p>Preço: %4 G</p>")
                               .arg(record->name, record->description,
                                    effect.isEmpty() ? tr("Equipamento") : effect)
                               .arg(record->data.value(QStringLiteral("price")).toInt()));
    m_useItem->setEnabled(category == QLatin1String("items") && !m_session.state().party().isEmpty());
}

void GameMenuDialog::useSelectedItem()
{
    if (!m_inventoryList->currentItem()) return;
    const QString id = m_inventoryList->currentItem()->data(Qt::UserRole).toString();
    const core::DatabaseRecord* item = databaseRecord(m_session.editor(), QStringLiteral("items"), id);
    if (!item || m_session.state().itemCount(id) <= 0) return;
    const int healHp = item->data.value(QStringLiteral("healHp")).toInt();
    const int healMp = item->data.value(QStringLiteral("healMp")).toInt();
    if (healHp == 0 && healMp == 0) {
        QMessageBox::information(this, tr("Usar item"), tr("Este item não possui um efeito de recuperação configurado."));
        return;
    }
    QVector<PartyMemberState*> targets;
    const QString scope = item->data.value(QStringLiteral("scope"), QStringLiteral("allyOne")).toString();
    if (scope == QLatin1String("allyAll")) {
        for (PartyMemberState& member : m_session.state().party()) targets.push_back(&member);
    } else if (PartyMemberState* member = m_session.state().partyMember(m_itemTarget->currentData().toString())) {
        targets.push_back(member);
    }
    for (PartyMemberState* member : targets) {
        const CombatStats stats = memberStats(m_session.editor(), *member);
        member->hp = qBound(0, member->hp + healHp, stats.maxHp);
        member->mp = qBound(0, member->mp + healMp, stats.maxMp);
    }
    if (item->data.value(QStringLiteral("consumable"), true).toBool())
        m_session.state().addItem(id, -1);
    rebuildAll();
}

void GameMenuDialog::rebuildEquipment()
{
    const QString actorId = m_equipActor->currentData().toString();
    const PartyMemberState* member = m_session.state().partyMember(actorId);
    auto fill = [this](QComboBox* combo, const QString& category, const QString& slot,
                       const QString& equipped) {
        QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(tr("(nenhum)"), QString());
        for (const core::DatabaseRecord& record : m_session.editor().database.value(category)) {
            const QString itemSlot = record.data.value(QStringLiteral("slot")).toString();
            if (!slot.isEmpty() && !itemSlot.isEmpty() && itemSlot != slot) continue;
            if (m_session.state().itemCount(record.id) <= 0 && record.id != equipped) continue;
            combo->addItem(tr("%1  [%2 no inventário]")
                               .arg(record.name).arg(m_session.state().itemCount(record.id)), record.id);
        }
        combo->setCurrentIndex(qMax(0, combo->findData(equipped)));
    };
    fill(m_weapon, QStringLiteral("weapons"), QStringLiteral("weapon"), member ? member->weaponId : QString());
    fill(m_armor, QStringLiteral("armors"), QStringLiteral("armor"), member ? member->armorId : QString());
    fill(m_accessory, QStringLiteral("armors"), QStringLiteral("accessory"), member ? member->accessoryId : QString());
    refreshEquipmentDetails();
}

void GameMenuDialog::refreshEquipmentDetails()
{
    const PartyMemberState* original = m_session.state().partyMember(m_equipActor->currentData().toString());
    if (!original) {
        m_equipDetails->setText(tr("O grupo está vazio."));
        return;
    }
    PartyMemberState preview = *original;
    preview.weaponId = m_weapon->currentData().toString();
    preview.armorId = m_armor->currentData().toString();
    preview.accessoryId = m_accessory->currentData().toString();
    const CombatStats before = memberStats(m_session.editor(), *original);
    const CombatStats after = memberStats(m_session.editor(), preview);
    auto delta = [](int oldValue, int newValue) {
        const int difference = newValue - oldValue;
        return difference == 0 ? QString() : QStringLiteral(" (%1%2)").arg(difference > 0 ? QStringLiteral("+") : QString()).arg(difference);
    };
    m_equipDetails->setText(tr("<h3>Atributos previstos</h3>"
                               "HP: %1%2<br>MP: %3%4<br>Ataque: %5%6<br>Defesa: %7%8<br>Agilidade: %9%10")
        .arg(after.maxHp).arg(delta(before.maxHp, after.maxHp))
        .arg(after.maxMp).arg(delta(before.maxMp, after.maxMp))
        .arg(after.attack).arg(delta(before.attack, after.attack))
        .arg(after.defense).arg(delta(before.defense, after.defense))
        .arg(after.agility).arg(delta(before.agility, after.agility)));
}

void GameMenuDialog::applyEquipment()
{
    const QString actorId = m_equipActor->currentData().toString();
    GameState trial = m_session.state();
    PartyMemberState* member = trial.partyMember(actorId);
    if (!member) return;
    QString error;
    if (!equipItem(m_session.editor(), trial, *member, QStringLiteral("weapon"),
                   m_weapon->currentData().toString(), &error) ||
        !equipItem(m_session.editor(), trial, *member, QStringLiteral("armor"),
                   m_armor->currentData().toString(), &error) ||
        !equipItem(m_session.editor(), trial, *member, QStringLiteral("accessory"),
                   m_accessory->currentData().toString(), &error)) {
        QMessageBox::warning(this, tr("Equipamento"), error);
    } else {
        m_session.state() = trial;
    }
    rebuildAll();
}

void GameMenuDialog::openSaveLoad(bool loadOnly)
{
    SaveLoadDialog dialog(m_session.editor(), loadOnly ? SaveLoadDialog::Mode::LoadOnly
                                                       : SaveLoadDialog::Mode::SaveAndLoad, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const int slot = dialog.selectedSlot();
    QString error;
    bool ok = false;
    if (loadOnly || dialog.selectedAction() == SaveLoadDialog::Action::Load) {
        ok = m_session.loadGame(slot, &error);
    } else {
        GameSaveSummary previous;
        if (readGameSaveSummary(gameSavePath(m_session.editor(), slot), m_session.editor(), &previous) &&
            QMessageBox::question(this, tr("Sobrescrever save"),
                                  tr("Sobrescrever o slot %1?").arg(slot)) != QMessageBox::Yes)
            return;
        ok = m_session.saveGame(slot, &error);
    }
    if (!ok) QMessageBox::warning(this, tr("Partida"), error);
    else rebuildAll();
}

} // namespace game
