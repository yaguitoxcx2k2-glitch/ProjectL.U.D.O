#pragma once

#include "game/GameState.h"
#include "game/BattleTypes.h"

#include <QDialog>
#include <QSet>

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
QT_END_NAMESPACE

namespace core { class Editor; struct DatabaseRecord; }

namespace game {


/// Batalha frontal por turnos inspirada no formato clássico de 256 cores.
class BattleDialog : public QDialog
{
public:
    BattleDialog(const core::Editor& ed, GameState& state, const QString& troopId,
                 bool allowEscape, QWidget* parent = nullptr);

    bool valid() const { return m_valid; }
    QString validationError() const { return m_validationError; }
    BattleResult battleResult() const { return m_result; }
    void reject() override;

private:
    struct Enemy {
        QString recordId;
        QString name;
        QString graphicPath;
        int maxHp = 1;
        int hp = 1;
        int maxMp = 0;
        int mp = 0;
        int attack = 1;
        int defense = 0;
        int agility = 1;
        int hitRate = 95;
        int evasion = 5;
        int critical = 5;
        QStringList skillIds;
        QHash<QString,int> stateTurns;
        int experience = 0;
        int gold = 0;
        QString lootId;
        int lootChance = 0;
    };

    void rebuildLists();
    void updateTurnLabel();
    PartyMemberState* currentActor();
    Enemy* selectedEnemy();
    bool allEnemiesDefeated() const;
    bool allPartyDefeated() const;
    void attack();
    void useSkill();
    void useItem();
    void defend();
    void tryEscape();
    void finishPlayerAction();
    void enemyTurn();
    void finishRoundStates();
    void addState(PartyMemberState& target,const QString& stateId,int chance);
    void addState(Enemy& target,const QString& stateId,int chance);
    void finishVictory();
    void finishDefeat();
    void appendLog(const QString& text);

    const core::Editor& m_ed;
    GameState& m_state;
    QVector<Enemy> m_enemies;
    bool m_allowEscape = true;
    bool m_valid = false;
    QString m_validationError;
    BattleResult m_result = BattleResult::Aborted;
    int m_actorTurn = 0;
    int m_round = 1;
    QSet<QString> m_defending;
    QLabel* m_turnLabel = nullptr;
    QListWidget* m_enemyList = nullptr;
    QVector<QLabel*> m_enemySprites;
    QListWidget* m_partyList = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton* m_attackButton = nullptr;
    QPushButton* m_skillButton = nullptr;
    QPushButton* m_itemButton = nullptr;
    QPushButton* m_defendButton = nullptr;
    QPushButton* m_escapeButton = nullptr;
};

} // namespace game
