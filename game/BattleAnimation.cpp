#include "BattleAnimation.h"

#include <QtMath>

namespace game {

QVariantMap BattleAnimationCue::toVariant() const
{
    return {
        {QStringLiteral("type"), type},
        {QStringLiteral("timeMs"), qMax(0, timeMs)},
        {QStringLiteral("durationMs"), qMax(1, durationMs)},
        {QStringLiteral("assetPath"), assetPath},
        {QStringLiteral("volume"), qBound(0, volume, 100)},
        {QStringLiteral("frames"), qMax(1, frames)},
        {QStringLiteral("columns"), qMax(1, columns)},
        {QStringLiteral("rows"), qMax(1, rows)},
        {QStringLiteral("loop"), loop},
        {QStringLiteral("width"), qMax(1.0, size.width())},
        {QStringLiteral("height"), qMax(1.0, size.height())},
        {QStringLiteral("offsetX"), offset.x()},
        {QStringLiteral("offsetY"), offset.y()},
        {QStringLiteral("opacity"), qBound(0.0, opacity, 1.0)},
        {QStringLiteral("scale"), qBound(0.01, scale, 20.0)},
        {QStringLiteral("rotation"), rotation},
        {QStringLiteral("color"), color.name(QColor::HexArgb)},
        {QStringLiteral("particleCount"), qBound(1, particleCount, 500)},
        {QStringLiteral("speed"), qBound(0.0, speed, 5000.0)},
        {QStringLiteral("spread"), qBound(0.0, spreadDegrees, 360.0)},
        {QStringLiteral("strength"), qBound(0.0, strength, 200.0)},
        {QStringLiteral("particleShape"), particleShape}
    };
}

BattleAnimationCue BattleAnimationCue::fromVariant(const QVariantMap& map)
{
    BattleAnimationCue cue;
    cue.type = map.value(QStringLiteral("type"), QStringLiteral("picture")).toString();
    cue.timeMs = qMax(0, map.value(QStringLiteral("timeMs"), 0).toInt());
    cue.durationMs = qMax(1, map.value(QStringLiteral("durationMs"), 250).toInt());
    cue.assetPath = map.value(QStringLiteral("assetPath")).toString();
    cue.volume = qBound(0, map.value(QStringLiteral("volume"), 90).toInt(), 100);
    cue.frames = qMax(1, map.value(QStringLiteral("frames"), 1).toInt());
    cue.columns = qMax(1, map.value(QStringLiteral("columns"), cue.frames).toInt());
    cue.rows = qMax(1, map.value(QStringLiteral("rows"), 1).toInt());
    cue.loop = map.value(QStringLiteral("loop"), false).toBool();
    cue.size = QSizeF(qMax(1.0, map.value(QStringLiteral("width"), 128.0).toDouble()),
                      qMax(1.0, map.value(QStringLiteral("height"), 128.0).toDouble()));
    cue.offset = QPointF(map.value(QStringLiteral("offsetX"), 0.0).toDouble(),
                         map.value(QStringLiteral("offsetY"), 0.0).toDouble());
    cue.opacity = qBound(0.0, map.value(QStringLiteral("opacity"), 1.0).toDouble(), 1.0);
    cue.scale = qBound(0.01, map.value(QStringLiteral("scale"), 1.0).toDouble(), 20.0);
    cue.rotation = map.value(QStringLiteral("rotation"), 0.0).toDouble();
    const QColor parsed(map.value(QStringLiteral("color"), QStringLiteral("#ffffffff")).toString());
    cue.color = parsed.isValid() ? parsed : QColor(Qt::white);
    cue.particleCount = qBound(1, map.value(QStringLiteral("particleCount"), 16).toInt(), 500);
    cue.speed = qBound(0.0, map.value(QStringLiteral("speed"), 80.0).toDouble(), 5000.0);
    cue.spreadDegrees = qBound(0.0, map.value(QStringLiteral("spread"), 360.0).toDouble(), 360.0);
    cue.strength = qBound(0.0, map.value(QStringLiteral("strength"), 6.0).toDouble(), 200.0);
    cue.particleShape = map.value(QStringLiteral("particleShape"), QStringLiteral("circle")).toString();
    return cue;
}

QVariantMap BattleAnimationDefinition::toData() const
{
    QVariantList list;
    list.reserve(cues.size());
    for (const BattleAnimationCue& cue : cues) list.push_back(cue.toVariant());
    return {
        {QStringLiteral("durationMs"), qMax(1, durationMs)},
        {QStringLiteral("hitFrameMs"), qBound(0, hitFrameMs, qMax(1, durationMs))},
        {QStringLiteral("cues"), list},
        // Compatibilidade: versões anteriores só exibiam um campo de frames.
        {QStringLiteral("frames"), qMax(1, durationMs / 100)}
    };
}

BattleAnimationDefinition BattleAnimationDefinition::fromData(const QVariantMap& data)
{
    BattleAnimationDefinition animation;
    const int legacyFrames = qMax(1, data.value(QStringLiteral("frames"), 4).toInt());
    animation.durationMs = qMax(1, data.value(QStringLiteral("durationMs"), legacyFrames * 100).toInt());
    animation.hitFrameMs = qBound(0, data.value(QStringLiteral("hitFrameMs"), animation.durationMs / 2).toInt(), animation.durationMs);
    const QVariantList list = data.value(QStringLiteral("cues")).toList();
    animation.cues.reserve(list.size());
    for (const QVariant& value : list) {
        const QVariantMap cueMap = value.toMap();
        if (!cueMap.isEmpty()) animation.cues.push_back(BattleAnimationCue::fromVariant(cueMap));
    }
    return animation;
}

BattleAnimationDefinition BattleAnimationDefinition::fromRecord(const core::DatabaseRecord* record)
{
    return record ? fromData(record->data) : BattleAnimationDefinition{};
}

int battleAnimationFrame(const BattleAnimationCue& cue, int cueElapsedMs)
{
    const int frames = qMax(1, qMin(cue.frames, qMax(1, cue.columns) * qMax(1, cue.rows)));
    if (frames <= 1) return 0;
    const int duration = qMax(1, cue.durationMs);
    if (cue.loop) {
        const int each = qMax(1, duration / frames);
        return (qMax(0, cueElapsedMs) / each) % frames;
    }
    const qreal progress = qBound(0.0, qreal(qMax(0, cueElapsedMs)) / duration, 0.999999);
    return qBound(0, int(qFloor(progress * frames)), frames - 1);
}

} // namespace game
