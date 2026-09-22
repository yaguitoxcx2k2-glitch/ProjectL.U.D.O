#include "Weather.h"

#include <QtGlobal>
#include <cmath>

namespace core {

WeatherKind weatherKindFromString(QString type)
{
    type = type.trimmed().toLower();
    if (type == QLatin1String("rain") || type == QLatin1String("chuva"))
        return WeatherKind::Rain;
    if (type == QLatin1String("snow") || type == QLatin1String("neve"))
        return WeatherKind::Snow;
    if (type == QLatin1String("storm") || type == QLatin1String("tempestade"))
        return WeatherKind::Storm;
    return WeatherKind::None;
}

QString weatherKindId(WeatherKind kind)
{
    switch (kind) {
    case WeatherKind::Rain:  return QStringLiteral("rain");
    case WeatherKind::Snow:  return QStringLiteral("snow");
    case WeatherKind::Storm: return QStringLiteral("storm");
    case WeatherKind::None:  break;
    }
    return QStringLiteral("none");
}

void WeatherState::setConfig(QString type, int requestedIntensity,
                             const QString& thunderSe, int requestedThunderVolume,
                             bool overrideMap)
{
    const WeatherKind nextKind = weatherKindFromString(type);
    const int nextIntensity = nextKind == WeatherKind::None
        ? 0 : qBound(1, requestedIntensity > 0 ? requestedIntensity : 50, 100);
    const int nextThunderVolume = qBound(0, requestedThunderVolume, 100);
    const QString nextThunder = thunderSe;
    const bool changed = kind != nextKind || intensity != nextIntensity ||
                         thunderSePath != nextThunder || thunderVolume != nextThunderVolume;

    kind = nextKind;
    intensity = nextIntensity;
    thunderSePath = nextThunder;
    thunderVolume = nextThunderVolume;
    runtimeOverride = overrideMap;
    if (changed || !std::isfinite(elapsed)) elapsed = 0.0;
}

void WeatherState::applyConfig(const WeatherState& config, bool overrideMap, bool restartPhase)
{
    const bool sameBefore = sameConfig(config);
    const double previousElapsed = std::isfinite(elapsed) ? qMax(0.0, elapsed) : 0.0;
    setConfig(config.typeId(), config.intensity, config.thunderSePath,
              config.thunderVolume, overrideMap);
    if (restartPhase) elapsed = 0.0;
    else if (sameBefore) elapsed = previousElapsed;
}

bool WeatherState::sameConfig(const WeatherState& other) const
{
    return kind == other.kind && intensity == other.intensity &&
           thunderSePath == other.thunderSePath && thunderVolume == other.thunderVolume;
}

} // namespace core
