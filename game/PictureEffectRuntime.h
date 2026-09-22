// ============================================================================
// PictureEffectRuntime.h — avaliação única dos efeitos temporais de Picture.
//
// O editor, o preview e o runtime usam os mesmos valores efetivos. O objetivo
// é impedir que Float/Breathing/Sway/Spin ganhem versões diferentes em cada
// consumidor. Os campos legados continuam em PictureDef por compatibilidade.
// ============================================================================
#pragma once

#include "core/Picture.h"

#include <QPointF>

namespace game {

struct PictureMotionSample {
    QPointF offset;
    double scaleAddX = 0.0;
    double scaleAddY = 0.0;
    double angleAdd = 0.0;
};

PictureMotionSample evaluatePictureMotion(const core::PictureDef& def, double ageSeconds);

} // namespace game
