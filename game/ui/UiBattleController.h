#pragma once

#include "game/BattleAnimation.h"
#include "game/BattleRules.h"
#include "game/BattleTypes.h"
#include "game/GameState.h"
#include "core/InputMap.h"

#include <QHash>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace core { class Editor; struct DatabaseRecord; }

namespace game::ui {

enum class UiBattleMode { Commands, EnemyTarget, SkillList, ItemList, AllyTarget, Result };

class UiBattleController {
public:
    struct Enemy {
        QString recordId;
        QString name;
        QString graphicPath;
        QImage image;
        qreal arenaX = -1.0; // -1 = formação automática legada
        qreal arenaY = 0.5;
        int maxHp = 1, hp = 1, maxMp = 0, mp = 0;
        int attack = 1, defense = 0, agility = 1, hitRate = 95, evasion = 5, critical = 5;
        QStringList skillIds;
        QHash<QString,int> stateTurns;
        int experience = 0, gold = 0;
        QString lootId;
        int lootChance = 0;
    };

    UiBattleController(const core::Editor& editor, GameState& state);

    bool open(const QString& troopId, bool allowEscape, QString* error = nullptr);
    void clear();
    void update(double dt);
    bool active() const { return m_active; }
    bool finished() const { return m_finished; }
    BattleResult result() const { return m_result; }
    BattleResult takeResult();
    bool handleAction(core::GameAction action);
    void selectIndex(int index);
    void refreshLocalization();

    UiBattleMode mode() const { return m_mode; }
    int selected() const { return m_selected; }
    int actorTurn() const { return m_actorTurn; }
    int round() const { return m_round; }
    bool allowEscape() const { return m_allowEscape; }
    const QVector<Enemy>& enemies() const { return m_enemies; }
    const QStringList& entries() const { return m_entries; }
    const QStringList& logLines() const { return m_log; }
    QString title() const;
    QString prompt() const;
    QString actorName() const;
    QImage background() const { return m_background; }
    QString resultSummary() const { return m_resultSummary; }
    const GameState& state() const { return m_state; }
    const core::Editor& editor() const { return m_ed; }

    // Animation runtime compartilhado por CPU/QRhi.
    bool animationActive() const { return m_animationActive; }
    int animationElapsedMs() const { return m_animationElapsedMs; }
    const BattleAnimationDefinition& animationDefinition() const { return m_animation; }
    bool animationTargetsParty() const { return m_animationTargetsParty; }
    const QVector<int>& animationTargets() const { return m_animationTargets; }
    QImage animationImage(const QString& projectRelativePath) const;
    QVector<BattleSoundCue> takeSoundEffects();

private:
    enum class PendingKind { None, Attack, Skill, Item };
    enum class ResolutionKind { None, PlayerAttack, PlayerSkill, PlayerItem, EnemyAttack, EnemySkill };
    struct PendingResolution {
        ResolutionKind kind = ResolutionKind::None;
        int sourceIndex = -1;
        int targetIndex = -1;
        QString recordId;
    };

    void rebuildEntries();
    void appendLog(const QString& text);
    PartyMemberState* currentActor();
    const PartyMemberState* currentActor() const;
    Enemy* selectedLivingEnemy();
    bool allEnemiesDefeated() const;
    bool allPartyDefeated() const;
    void activateSelected();
    void back();
    void attackSelected();
    void chooseSkill();
    void chooseItem();
    void applySkill(const core::DatabaseRecord& skill, int targetIndex = -1);
    void applyItem(const core::DatabaseRecord& item, int targetIndex = -1);
    void resolvePlayerAttack(int targetIndex);
    void resolvePlayerSkill(const core::DatabaseRecord& skill, int targetIndex);
    void resolvePlayerItem(const core::DatabaseRecord& item, int targetIndex);
    void resolveEnemyAction(int enemyIndex, const core::DatabaseRecord* skill, int targetIndex);
    void defend();
    void tryEscape();
    void finishPlayerAction();
    void startEnemyTurn();
    void continueEnemyTurn();
    const core::DatabaseRecord* chooseEnemySkill(int enemyIndex) const;
    void addState(PartyMemberState& target, const QString& stateId, int chance);
    void addState(Enemy& target, const QString& stateId, int chance);
    void finishRoundStates();
    void finishVictory();
    void finishDefeat();
    void showResult(BattleResult result, const QString& summary);
    QVector<const core::DatabaseRecord*> actorSkills() const;
    QVector<const core::DatabaseRecord*> usableItems() const;
    int firstLivingEnemy() const;

    QString playerAttackAnimationId() const;
    QString enemyAttackAnimationId(int enemyIndex) const;
    QVector<int> animationTargetsForScope(const QString& scope, int targetIndex, bool playerSource) const;
    void beginAnimatedResolution(const QString& animationId, bool targetsParty,
                                 const QVector<int>& targets, const PendingResolution& pending);
    void resolvePendingHit();
    void finishAnimatedResolution();
    void queueAnimationSounds(int previousMs, int currentMs);

    void processBattleEvents(const QString& trigger);
    bool battleEventConditionMatches(const BattleEventRule& rule, const QString& trigger) const;
    void executeBattleEvent(const BattleEventRule& rule);

    const core::Editor& m_ed;
    GameState& m_state;
    QVector<Enemy> m_enemies;
    bool m_active = false;
    bool m_finished = false;
    bool m_allowEscape = true;
    BattleResult m_result = BattleResult::Aborted;
    UiBattleMode m_mode = UiBattleMode::Commands;
    PendingKind m_pending = PendingKind::None;
    QString m_pendingId;
    int m_selected = 0;
    int m_actorTurn = 0;
    int m_round = 1;
    QSet<QString> m_defending;
    QStringList m_entries;
    QStringList m_log;
    QString m_troopId;
    QString m_troopName;
    QString m_resultSummary;
    QImage m_background;

    // Animation state.
    BattleAnimationDefinition m_animation;
    bool m_animationActive = false;
    bool m_animationHitResolved = false;
    int m_animationElapsedMs = 0;
    bool m_animationTargetsParty = false;
    QVector<int> m_animationTargets;
    PendingResolution m_pendingResolution;
    QVector<BattleSoundCue> m_soundQueue;
    mutable QHash<QString,QImage> m_animationImageCache;

    // Enemy turn state machine (allows animations between individual actions).
    bool m_enemyTurnInProgress = false;
    int m_enemyTurnCursor = 0;

    // Troop battle events.
    QVector<BattleEventRule> m_battleEvents;
    QSet<QString> m_firedBattleEvents;
    QHash<QString,int> m_eventLastRound;
};

} // namespace game::ui
