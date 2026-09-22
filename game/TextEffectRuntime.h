#pragma once

#include "core/TextEffects.h"

#include <QColor>
#include <QPointF>

namespace game {

struct TextEffectSample {
    QPointF offset;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotation = 0.0;
    double opacity = 1.0;
    QColor colorOverride;
    double brighten = 0.0;
};

/// Pure deterministic evaluator shared by editor previews, QPainter runtime and
/// the QRhi texture path. No preset-name branching lives in the renderer.
TextEffectSample evaluateTextEffects(const core::TextEffectStack& stack,
                                     double visibleTimeSec, double exitTimeSec,
                                     int characterIndex, int wordIndex,
                                     int characterCount = 1, int wordCount = 1,
                                     double entranceNotBeforeSec = -1.0,
                                     double loopStartOverrideSec = -1.0);

/// Returns the time at which the last staggered target reaches the end of an
/// entrance/exit tween. Used by previews and tests; no fake timer required.
double textEffectPhaseSpanSec(const core::TextEffectPhaseSpec& phase,
                              int characterCount, int wordCount);

/// Typewriter is a reveal policy, not a transform. Presets may request it and
/// consumers can use this helper without teaching the renderer a second text
/// state machine. Returns <=0 when the phase is not a typewriter preset.
double textEffectTypewriterCharsPerSecond(const core::TextEffectPhaseSpec& phase);

/// Shared reveal policy for the Typewriter motion. Consumers that do not own
/// a message state machine (Choices/Picture Text/HUD) use this directly, while
/// Messages can keep their pause/wait semantics and still share the same speed.
bool textEffectTargetRevealed(const core::TextEffectPhaseSpec& phase,
                              double visibleTimeSec, int characterIndex, int wordIndex,
                              int characterCount = 1, int wordCount = 1);

} // namespace game
