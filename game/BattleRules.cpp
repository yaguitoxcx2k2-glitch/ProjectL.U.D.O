#include "BattleRules.h"

#include <QUuid>

namespace game {

QVariantMap BattleAiRule::toVariant() const
{
    return {{QStringLiteral("skillId"), skillId},
            {QStringLiteral("weight"), qBound(1, weight, 9999)},
            {QStringLiteral("hpMin"), qBound(0, hpMinPercent, 100)},
            {QStringLiteral("hpMax"), qBound(0, hpMaxPercent, 100)},
            {QStringLiteral("mpMin"), qMax(0, mpMin)},
            {QStringLiteral("everyTurns"), qMax(1, everyTurns)},
            {QStringLiteral("turnOffset"), qMax(0, turnOffset)},
            {QStringLiteral("requiredStateId"), requiredStateId}};
}

BattleAiRule BattleAiRule::fromVariant(const QVariantMap& map)
{
    BattleAiRule rule;
    rule.skillId = map.value(QStringLiteral("skillId")).toString();
    rule.weight = qBound(1, map.value(QStringLiteral("weight"), 10).toInt(), 9999);
    rule.hpMinPercent = qBound(0, map.value(QStringLiteral("hpMin"), 0).toInt(), 100);
    rule.hpMaxPercent = qBound(rule.hpMinPercent, map.value(QStringLiteral("hpMax"), 100).toInt(), 100);
    rule.mpMin = qMax(0, map.value(QStringLiteral("mpMin"), 0).toInt());
    rule.everyTurns = qMax(1, map.value(QStringLiteral("everyTurns"), 1).toInt());
    rule.turnOffset = qMax(0, map.value(QStringLiteral("turnOffset"), 0).toInt());
    rule.requiredStateId = map.value(QStringLiteral("requiredStateId")).toString();
    return rule;
}

QVector<BattleAiRule> battleAiRules(const core::DatabaseRecord* enemy)
{
    QVector<BattleAiRule> result;
    if (!enemy) return result;
    for (const QVariant& value : enemy->data.value(QStringLiteral("aiRules")).toList()) {
        const QVariantMap map = value.toMap();
        if (map.isEmpty()) continue;
        const BattleAiRule rule = BattleAiRule::fromVariant(map);
        if (!rule.skillId.isEmpty()) result.push_back(rule);
    }
    return result;
}

QVector<BattleAiRule> matchingBattleAiRules(const QVector<BattleAiRule>& rules,
                                            int hp, int maxHp, int mp, int round,
                                            const QSet<QString>& states)
{
    QVector<BattleAiRule> result;
    const int hpPercent = maxHp > 0 ? qBound(0, hp * 100 / maxHp, 100) : 0;
    for (const BattleAiRule& rule : rules) {
        if (hpPercent < rule.hpMinPercent || hpPercent > rule.hpMaxPercent) continue;
        if (mp < rule.mpMin) continue;
        if (!rule.requiredStateId.isEmpty() && !states.contains(rule.requiredStateId)) continue;
        const int relative = qMax(0, round - 1 - rule.turnOffset);
        if (relative % qMax(1, rule.everyTurns) != 0) continue;
        result.push_back(rule);
    }
    return result;
}

QVariantMap BattleEventRule::toVariant() const
{
    const QString stableId = id.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    return {{QStringLiteral("id"), stableId}, {QStringLiteral("trigger"), trigger},
            {QStringLiteral("value"), value}, {QStringLiteral("once"), once},
            {QStringLiteral("enemyId"), enemyId}, {QStringLiteral("action"), action}};
}

BattleEventRule BattleEventRule::fromVariant(const QVariantMap& map)
{
    BattleEventRule rule;
    rule.id = map.value(QStringLiteral("id")).toString();
    if (rule.id.isEmpty()) rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rule.trigger = map.value(QStringLiteral("trigger"), QStringLiteral("battleStart")).toString();
    rule.value = qMax(0, map.value(QStringLiteral("value"), 0).toInt());
    rule.once = map.value(QStringLiteral("once"), true).toBool();
    rule.enemyId = map.value(QStringLiteral("enemyId")).toString();
    rule.action = map.value(QStringLiteral("action")).toMap();
    return rule;
}

QVector<BattleEventRule> battleEventRules(const core::DatabaseRecord* troop)
{
    QVector<BattleEventRule> result;
    if (!troop) return result;
    for (const QVariant& value : troop->data.value(QStringLiteral("battleEvents")).toList()) {
        const QVariantMap map = value.toMap();
        if (!map.isEmpty()) result.push_back(BattleEventRule::fromVariant(map));
    }
    return result;
}

QVariantMap TroopEnemySlot::toVariant() const
{
    return {{QStringLiteral("enemyId"), enemyId},
            {QStringLiteral("x"), qBound(0.0, x, 1.0)},
            {QStringLiteral("y"), qBound(0.0, y, 1.0)}};
}

TroopEnemySlot TroopEnemySlot::fromVariant(const QVariantMap& map)
{
    TroopEnemySlot slot;
    slot.enemyId = map.value(QStringLiteral("enemyId")).toString();
    slot.x = qBound(0.0, map.value(QStringLiteral("x"), 0.5).toDouble(), 1.0);
    slot.y = qBound(0.0, map.value(QStringLiteral("y"), 0.5).toDouble(), 1.0);
    return slot;
}

QVector<TroopEnemySlot> troopEnemySlots(const core::DatabaseRecord* troop)
{
    QVector<TroopEnemySlot> result;
    if (!troop) return result;
    for (const QVariant& value : troop->data.value(QStringLiteral("enemySlots")).toList()) {
        const QVariantMap map = value.toMap();
        if (map.isEmpty()) continue;
        const TroopEnemySlot slot = TroopEnemySlot::fromVariant(map);
        if (!slot.enemyId.isEmpty()) result.push_back(slot);
    }
    return result;
}

} // namespace game
