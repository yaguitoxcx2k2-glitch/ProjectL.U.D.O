#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace core {

// Declarative, backend-agnostic rich-text gradient. An empty color list means
// "inherit/solid". Keeping it in core lets Messages, Subtitles, Pictures,
// Choices and UI widgets persist the exact same style without renderer copies.
struct TextGradientSpec {
    QString direction = QStringLiteral("vertical"); // vertical/horizontal/diagonal
    QVector<QColor> colors;

    bool enabled() const { return colors.size() >= 2; }
    bool operator==(const TextGradientSpec& o) const { return direction == o.direction && colors == o.colors; }
    bool operator!=(const TextGradientSpec& o) const { return !(*this == o); }
    QVariantMap toVariantMap() const;
    static TextGradientSpec fromVariantMap(const QVariantMap& map);
};

// One generic animation phase. Presets are merely values for this struct; the
// runtime never switches on a preset name. This is the central contract that
// avoids separate Wave/Impact/Breathing implementations in each consumer.
struct TextEffectPhaseSpec {
    bool enabled = false;
    QString target = QStringLiteral("character"); // character/word/block
    QString motion = QStringLiteral("tween");     // tween/wave/shake/float/jitter/pulse/rainbow/glow/sweep/typewriter
    QString easing = QStringLiteral("linear");
    int durationMs = 600;
    int delayMs = 0;
    int staggerMs = 0;
    /// Fade curto aplicado a cada alvo quando ele é revelado (mensagem/typewriter).
    bool fadeOnReveal = false;
    int fadeRevealMs = 120;

    double opacityFrom = 1.0;
    double opacityTo = 1.0;
    double translateXFrom = 0.0;
    double translateXTo = 0.0;
    double translateYFrom = 0.0;
    double translateYTo = 0.0;
    double scaleXFrom = 1.0;
    double scaleXTo = 1.0;
    double scaleYFrom = 1.0;
    double scaleYTo = 1.0;
    double rotationFrom = 0.0;
    double rotationTo = 0.0;

    // Procedural motion parameters. `amount` is pixels/percent/intensity
    // depending on the motion; `frequency` is cycles-ish per second.
    double amount = 0.0;
    double frequency = 1.0;

    // Loop behavior is intentionally independent from the motion/preset.
    QString loopMode = QStringLiteral("once"); // once/while-visible/count/ping-pong
    int loopCount = 1;

    QVariantMap toVariantMap() const;
    static TextEffectPhaseSpec fromVariantMap(const QVariantMap& map);
};

struct TextEffectStack {
    TextEffectPhaseSpec entrance;
    TextEffectPhaseSpec loop;
    TextEffectPhaseSpec exit;

    bool enabled() const { return entrance.enabled || loop.enabled || exit.enabled; }
    QVariantMap toVariantMap() const;
    static TextEffectStack fromVariantMap(const QVariantMap& map);
};

struct TextEffectPreset {
    QString id;
    QString name;
    TextEffectPhaseSpec phase;

    QVariantMap toVariantMap() const;
    static TextEffectPreset fromVariantMap(const QVariantMap& map, const QString& fallbackId = {});
};

QVector<TextEffectPreset> builtInTextEffectPresets();
TextEffectPhaseSpec builtInTextEffectPreset(const QString& id, bool* found = nullptr);
QString textEffectTargetLabel(const QString& id);
QString textEffectLoopLabel(const QString& id);
QString textEffectEasingLabel(const QString& id);

} // namespace core
