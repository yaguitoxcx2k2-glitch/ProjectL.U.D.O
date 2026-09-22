#include "RpgSystem.h"

#include "core/Editor.h"

#include <QCoreApplication>
#include <QSettings>

namespace game {

const core::DatabaseRecord* databaseRecord(const core::Editor& ed,
                                           const QString& category,
                                           const QString& id)
{
    if (id.isEmpty()) return nullptr;
    const auto it = ed.database.constFind(category);
    if (it == ed.database.cend()) return nullptr;
    for (const core::DatabaseRecord& record : it.value())
        if (record.id == id) return &record;
    return nullptr;
}

const core::DatabaseRecord* databaseRecordByNumber(const core::Editor& ed,
                                                   const QString& category,
                                                   int number)
{
    const auto it = ed.database.constFind(category);
    if (it == ed.database.cend()) return nullptr;
    for (const core::DatabaseRecord& record : it.value())
        if (record.number == number) return &record;
    return nullptr;
}

QString databaseRecordName(const core::Editor& ed, const QString& category,
                           const QString& id, const QString& fallback)
{
    const core::DatabaseRecord* record = databaseRecord(ed, category, id);
    if (!record || record->name.isEmpty()) return fallback;
    return core::resolvePlayerText(ed.localization,
                                   record->data.value(QStringLiteral("nameTextKey")).toString(),
                                   record->name);
}

QString databaseRecordDescription(const core::Editor& ed, const QString& category,
                                  const QString& id, const QString& fallback)
{
    const core::DatabaseRecord* record = databaseRecord(ed, category, id);
    if (!record || record->description.isEmpty()) return fallback;
    return core::resolvePlayerText(ed.localization,
                                   record->data.value(QStringLiteral("descriptionTextKey")).toString(),
                                   record->description);
}

namespace {

int valueFrom(const core::DatabaseRecord* primary, const core::DatabaseRecord* fallback,
              const QString& key, int defaultValue)
{
    if (primary && primary->data.contains(key)) return primary->data.value(key).toInt();
    if (fallback && fallback->data.contains(key)) return fallback->data.value(key).toInt();
    return defaultValue;
}

void addEquipment(const core::Editor& ed, const QString& category, const QString& id,
                  CombatStats& stats)
{
    const core::DatabaseRecord* item = databaseRecord(ed, category, id);
    if (!item) return;
    stats.maxHp += item->data.value(QStringLiteral("hp"), 0).toInt();
    stats.maxMp += item->data.value(QStringLiteral("mp"), 0).toInt();
    stats.attack += item->data.value(QStringLiteral("attack"), 0).toInt();
    stats.defense += item->data.value(QStringLiteral("defense"), 0).toInt();
    stats.agility += item->data.value(QStringLiteral("agility"), 0).toInt();
}

} // namespace

CombatStats memberStats(const core::Editor& ed, const PartyMemberState& member)
{
    const core::DatabaseRecord* actor = databaseRecord(ed, QStringLiteral("actors"), member.actorId);
    const QString classId = actor ? actor->data.value(QStringLiteral("classId")).toString() : QString();
    const core::DatabaseRecord* klass = databaseRecord(ed, QStringLiteral("classes"), classId);
    const int level = qMax(1, member.level);
    CombatStats stats;
    stats.maxHp = valueFrom(actor, klass, QStringLiteral("hp"), 100) +
                  (level - 1) * valueFrom(actor, klass, QStringLiteral("hpGrowth"), 12);
    stats.maxMp = valueFrom(actor, klass, QStringLiteral("mp"), 30) +
                  (level - 1) * valueFrom(actor, klass, QStringLiteral("mpGrowth"), 4);
    stats.attack = valueFrom(actor, klass, QStringLiteral("attack"), 10) +
                   (level - 1) * valueFrom(actor, klass, QStringLiteral("attackGrowth"), 2);
    stats.defense = valueFrom(actor, klass, QStringLiteral("defense"), 8) +
                    (level - 1) * valueFrom(actor, klass, QStringLiteral("defenseGrowth"), 2);
    stats.agility = valueFrom(actor, klass, QStringLiteral("agility"), 10) +
                    (level - 1) * valueFrom(actor, klass, QStringLiteral("agilityGrowth"), 1);
    addEquipment(ed, QStringLiteral("weapons"), member.weaponId, stats);
    addEquipment(ed, QStringLiteral("armors"), member.armorId, stats);
    addEquipment(ed, QStringLiteral("armors"), member.accessoryId, stats);
    double attackRate=1.0,defenseRate=1.0,agilityRate=1.0;
    for(const QString& stateId:member.states){
        if(const core::DatabaseRecord* state=databaseRecord(ed,QStringLiteral("states"),stateId)){
            attackRate*=qBound(0.0,state->data.value(QStringLiteral("attackRate"),1.0).toDouble(),10.0);
            defenseRate*=qBound(0.0,state->data.value(QStringLiteral("defenseRate"),1.0).toDouble(),10.0);
            agilityRate*=qBound(0.0,state->data.value(QStringLiteral("agilityRate"),1.0).toDouble(),10.0);
        }
    }
    stats.attack=qRound(stats.attack*attackRate);
    stats.defense=qRound(stats.defense*defenseRate);
    stats.agility=qRound(stats.agility*agilityRate);
    stats.maxHp = qMax(1, stats.maxHp);
    stats.maxMp = qMax(0, stats.maxMp);
    stats.attack = qMax(1, stats.attack);
    stats.defense = qMax(0, stats.defense);
    stats.agility = qMax(1, stats.agility);
    return stats;
}

int experienceForLevel(int level)
{
    level = qBound(1, level, 999);
    return (level - 1) * (level - 1) * 25;
}

int experienceToNextLevel(const core::Editor& ed, const PartyMemberState& member)
{
    const core::DatabaseRecord* actor = databaseRecord(ed, QStringLiteral("actors"), member.actorId);
    const int maximum = qBound(1, actor ? actor->data.value(QStringLiteral("maxLevel"), 99).toInt() : 99, 999);
    if (member.level >= maximum) return 0;
    return qMax(0, experienceForLevel(member.level + 1) - member.experience);
}

void normalizeMember(const core::Editor& ed, PartyMemberState& member)
{
    const core::DatabaseRecord* actor = databaseRecord(ed, QStringLiteral("actors"), member.actorId);
    const int maximum = qBound(1, actor ? actor->data.value(QStringLiteral("maxLevel"), 99).toInt() : 99, 999);
    member.level = qBound(1, member.level, maximum);
    member.experience = qMax(0, member.experience);
    const CombatStats stats = memberStats(ed, member);
    member.hp = qBound(0, member.hp, stats.maxHp);
    member.mp = qBound(0, member.mp, stats.maxMp);
    member.states.removeDuplicates();
    for(const QString& stateId:member.states)if(!member.stateTurns.contains(stateId)){
        const core::DatabaseRecord* state=databaseRecord(ed,QStringLiteral("states"),stateId);
        member.stateTurns[stateId]=qMax(0,state?state->data.value(QStringLiteral("duration"),0).toInt():0);
    }
    for(auto it=member.stateTurns.begin();it!=member.stateTurns.end();)
        if(!member.states.contains(it.key()))it=member.stateTurns.erase(it);else{it.value()=qMax(0,it.value());++it;}
}

bool setExperience(const core::Editor& ed, PartyMemberState& member, int total,
                   QStringList* learnedMessages)
{
    const core::DatabaseRecord* actor = databaseRecord(ed, QStringLiteral("actors"), member.actorId);
    const int maximum = qBound(1, actor ? actor->data.value(QStringLiteral("maxLevel"), 99).toInt() : 99, 999);
    const int oldLevel = member.level;
    const CombatStats before = memberStats(ed, member);
    member.experience = qMax(0, total);
    member.level = 1;
    while (member.level < maximum && member.experience >= experienceForLevel(member.level + 1)) ++member.level;
    if (member.level != oldLevel) {
        const CombatStats after = memberStats(ed, member);
        if (member.hp > 0)
            member.hp = qBound(1, member.hp + after.maxHp - before.maxHp, after.maxHp);
        member.mp = qBound(0, member.mp + after.maxMp - before.maxMp, after.maxMp);
        if (learnedMessages && member.level > oldLevel) {
            const QString name = databaseRecordName(ed, QStringLiteral("actors"), member.actorId,
                                                    QCoreApplication::translate("RpgSystem", "Personagem"));
            learnedMessages->push_back(QCoreApplication::translate("RpgSystem", "%1 chegou ao nível %2!")
                                           .arg(name).arg(member.level));
        }
    }
    normalizeMember(ed, member);
    return member.level != oldLevel;
}

bool gainExperience(const core::Editor& ed, PartyMemberState& member, int amount,
                    QStringList* learnedMessages)
{
    if (amount <= 0) return false;
    const qint64 sum = qint64(member.experience) + qint64(amount);
    const qint64 total = sum > 2147483647LL ? 2147483647LL : sum;
    return setExperience(ed, member, int(total), learnedMessages);
}

bool equipItem(const core::Editor& ed, GameState& state, PartyMemberState& member,
               const QString& slotId, const QString& itemId, QString* error)
{
    QString* equipped = nullptr;
    QString category;
    if (slotId == QLatin1String("weapon")) {
        equipped = &member.weaponId;
        category = QStringLiteral("weapons");
    } else if (slotId == QLatin1String("armor")) {
        equipped = &member.armorId;
        category = QStringLiteral("armors");
    } else if (slotId == QLatin1String("accessory")) {
        equipped = &member.accessoryId;
        category = QStringLiteral("armors");
    } else {
        if (error) *error = QCoreApplication::translate("RpgSystem", "Tipo de equipamento inválido.");
        return false;
    }
    if (*equipped == itemId) return true;
    if (!itemId.isEmpty()) {
        const core::DatabaseRecord* item = databaseRecord(ed, category, itemId);
        if (!item || state.itemCount(itemId) <= 0) {
            if (error) *error = QCoreApplication::translate("RpgSystem", "O equipamento não está no inventário.");
            return false;
        }
        const QString configuredSlot = item->data.value(QStringLiteral("slot")).toString();
        if (!configuredSlot.isEmpty() && configuredSlot != slotId) {
            if (error) *error = QCoreApplication::translate("RpgSystem", "Este item não pode ser usado neste espaço.");
            return false;
        }
        state.addItem(itemId, -1);
    }
    if (!equipped->isEmpty()) state.addItem(*equipped, 1);
    *equipped = itemId;
    normalizeMember(ed, member);
    return true;
}

} // namespace game
