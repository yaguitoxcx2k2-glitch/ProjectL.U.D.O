#pragma once

#include "core/Database.h"

#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace game {

struct BattleAiRule
{
    QString skillId;
    int weight = 10;
    int hpMinPercent = 0;
    int hpMaxPercent = 100;
    int mpMin = 0;
    int everyTurns = 1;
    int turnOffset = 0;
    QString requiredStateId;

    QVariantMap toVariant() const;
    static BattleAiRule fromVariant(const QVariantMap& map);
};

QVector<BattleAiRule> battleAiRules(const core::DatabaseRecord* enemy);

/// Filtra regras de IA para o estado atual. O sorteio ponderado fica no
/// controller; esta função é determinística e testável.
QVector<BattleAiRule> matchingBattleAiRules(const QVector<BattleAiRule>& rules,
                                            int hp, int maxHp, int mp, int round,
                                            const QSet<QString>& states);

struct BattleEventRule
{
    QString id;
    QString trigger = QStringLiteral("battleStart");
    int value = 0; // rodada ou limiar percentual, conforme trigger
    bool once = true;
    QString enemyId; // opcional para enemyHpBelow
    QVariantMap action;

    QVariantMap toVariant() const;
    static BattleEventRule fromVariant(const QVariantMap& map);
};

QVector<BattleEventRule> battleEventRules(const core::DatabaseRecord* troop);

struct TroopEnemySlot
{
    QString enemyId;
    qreal x = 0.5; // coordenadas normalizadas dentro da arena
    qreal y = 0.5;

    QVariantMap toVariant() const;
    static TroopEnemySlot fromVariant(const QVariantMap& map);
};

QVector<TroopEnemySlot> troopEnemySlots(const core::DatabaseRecord* troop);

} // namespace game
