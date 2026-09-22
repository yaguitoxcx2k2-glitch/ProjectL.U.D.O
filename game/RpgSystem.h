#pragma once

#include "core/Database.h"
#include "game/GameState.h"

namespace core { class Editor; }

namespace game {

struct CombatStats
{
    int maxHp = 1;
    int maxMp = 0;
    int attack = 1;
    int defense = 0;
    int agility = 1;
};

const core::DatabaseRecord* databaseRecord(const core::Editor& ed,
                                           const QString& category,
                                           const QString& id);
const core::DatabaseRecord* databaseRecordByNumber(const core::Editor& ed,
                                                   const QString& category,
                                                   int number);
QString databaseRecordName(const core::Editor& ed, const QString& category,
                           const QString& id, const QString& fallback = QString());
QString databaseRecordDescription(const core::Editor& ed, const QString& category,
                                  const QString& id, const QString& fallback = QString());

CombatStats memberStats(const core::Editor& ed, const PartyMemberState& member);
int experienceForLevel(int level);
int experienceToNextLevel(const core::Editor& ed, const PartyMemberState& member);
bool setExperience(const core::Editor& ed, PartyMemberState& member, int total,
                   QStringList* learnedMessages = nullptr);
bool gainExperience(const core::Editor& ed, PartyMemberState& member, int amount,
                    QStringList* learnedMessages = nullptr);
void normalizeMember(const core::Editor& ed, PartyMemberState& member);
bool equipItem(const core::Editor& ed, GameState& state, PartyMemberState& member,
               const QString& slotId, const QString& itemId, QString* error = nullptr);

} // namespace game
