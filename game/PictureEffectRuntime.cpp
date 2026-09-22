#include "PictureEffectRuntime.h"

#include <QtMath>
#include <cmath>

namespace game {
namespace {
double wave(double age, double speed)
{
    if (!std::isfinite(age) || !std::isfinite(speed) || qFuzzyIsNull(speed)) return 0.0;
    return std::sin(age * speed * 2.0 * M_PI);
}
} // namespace

PictureMotionSample evaluatePictureMotion(const core::PictureDef& def, double ageSeconds)
{
    PictureMotionSample s;
    const double age = std::isfinite(ageSeconds) ? qMax(0.0, ageSeconds) : 0.0;
    s.offset.setX(def.swaySpeed != 0.0 ? wave(age, def.swaySpeed) * def.swayRange : 0.0);
    s.offset.setY(def.floatSpeed != 0.0 ? wave(age, def.floatSpeed) * def.floatRange : 0.0);
    const double pulse = def.pulseSpeed != 0.0 ? wave(age, def.pulseSpeed) * def.pulseRange : 0.0;
    s.scaleAddX = pulse;
    s.scaleAddY = pulse;
    s.angleAdd = def.spinSpeed * age;
    return s;
}

} // namespace game
