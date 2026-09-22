#include "RuntimePersistence.h"

#include <QtGlobal>
#include <cmath>

namespace game {
namespace {

double finite(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

double finiteNonNegative(double value, double fallback = 0.0)
{
    value = finite(value, fallback);
    return qMax(0.0, value);
}

double runtimeZoom(double value, double fallback = 2.0)
{
    return qBound(0.25, finite(value, fallback), 8.0);
}

QPointF finitePoint(double x, double y, const QPointF& fallback = {})
{
    return QPointF(finite(x, fallback.x()), finite(y, fallback.y()));
}

QString normalizedEase(QString ease)
{
    ease = ease.trimmed();
    if (ease == QLatin1String("linear") || ease == QLatin1String("smooth") ||
        ease == QLatin1String("easeIn") || ease == QLatin1String("easeOut") ||
        ease == QLatin1String("slide"))
        return ease;
    return QStringLiteral("smooth");
}

} // namespace

QJsonObject RuntimeCameraState::toJson(double currentZoom) const
{
    return QJsonObject{
        {QStringLiteral("active"), active},
        {QStringLiteral("moving"), moving},
        {QStringLiteral("tweenCenter"), tweenCenter},
        {QStringLiteral("tweenZoom"), tweenZoom},
        {QStringLiteral("follow"), follow},
        {QStringLiteral("target"), target},
        {QStringLiteral("params"), QJsonObject::fromVariantMap(params)},
        {QStringLiteral("centerX"), center.x()},
        {QStringLiteral("centerY"), center.y()},
        {QStringLiteral("zoom"), runtimeZoom(currentZoom)},
        {QStringLiteral("startCenterX"), startCenter.x()},
        {QStringLiteral("startCenterY"), startCenter.y()},
        {QStringLiteral("endCenterX"), endCenter.x()},
        {QStringLiteral("endCenterY"), endCenter.y()},
        {QStringLiteral("startZoom"), runtimeZoom(startZoom)},
        {QStringLiteral("endZoom"), runtimeZoom(endZoom)},
        {QStringLiteral("elapsed"), finiteNonNegative(elapsed)},
        {QStringLiteral("duration"), finiteNonNegative(duration)},
        {QStringLiteral("ease"), normalizedEase(ease)},
        {QStringLiteral("followSpeed"), qBound(0.1, finite(followSpeed, 6.0), 30.0)},
        {QStringLiteral("deadzone"), finiteNonNegative(deadzone)},
        {QStringLiteral("saved"), saved},
        {QStringLiteral("savedCenterX"), savedCenter.x()},
        {QStringLiteral("savedCenterY"), savedCenter.y()},
        {QStringLiteral("savedZoom"), runtimeZoom(savedZoom)}
    };
}

void RuntimeCameraState::fromJson(const QJsonObject& json, double* currentZoom)
{
    *this = RuntimeCameraState();
    if (json.isEmpty()) {
        if (currentZoom) *currentZoom = 2.0;
        return;
    }

    active = json.value(QStringLiteral("active")).toBool(false);
    tweenCenter = json.value(QStringLiteral("tweenCenter")).toBool(true);
    tweenZoom = json.value(QStringLiteral("tweenZoom")).toBool(true);
    follow = json.value(QStringLiteral("follow")).toBool(false);
    target = json.value(QStringLiteral("target")).toString();
    params = json.value(QStringLiteral("params")).toObject().toVariantMap();
    center = finitePoint(json.value(QStringLiteral("centerX")).toDouble(),
                         json.value(QStringLiteral("centerY")).toDouble());
    const double zoom = runtimeZoom(json.value(QStringLiteral("zoom")).toDouble(2.0));
    if (currentZoom) *currentZoom = zoom;

    // RC2.18 e anteriores só gravavam center/zoom. Na ausência dos campos de
    // transição, o estado antigo vira uma câmera estável sem salto.
    startCenter = finitePoint(json.value(QStringLiteral("startCenterX")).toDouble(center.x()),
                              json.value(QStringLiteral("startCenterY")).toDouble(center.y()), center);
    endCenter = finitePoint(json.value(QStringLiteral("endCenterX")).toDouble(center.x()),
                            json.value(QStringLiteral("endCenterY")).toDouble(center.y()), center);
    startZoom = runtimeZoom(json.value(QStringLiteral("startZoom")).toDouble(zoom), zoom);
    endZoom = runtimeZoom(json.value(QStringLiteral("endZoom")).toDouble(zoom), zoom);
    duration = finiteNonNegative(json.value(QStringLiteral("duration")).toDouble());
    elapsed = qMin(duration, finiteNonNegative(json.value(QStringLiteral("elapsed")).toDouble()));
    ease = normalizedEase(json.value(QStringLiteral("ease")).toString(QStringLiteral("smooth")));
    moving = json.value(QStringLiteral("moving")).toBool(false) && duration > 0.0 && elapsed < duration;
    if (moving) active = true;
    else {
        startCenter = endCenter = center;
        startZoom = endZoom = zoom;
        elapsed = duration = 0.0;
    }

    followSpeed = qBound(0.1, finite(json.value(QStringLiteral("followSpeed")).toDouble(6.0), 6.0), 30.0);
    deadzone = finiteNonNegative(json.value(QStringLiteral("deadzone")).toDouble());
    saved = json.value(QStringLiteral("saved")).toBool(false);
    savedCenter = finitePoint(json.value(QStringLiteral("savedCenterX")).toDouble(center.x()),
                              json.value(QStringLiteral("savedCenterY")).toDouble(center.y()), center);
    savedZoom = runtimeZoom(json.value(QStringLiteral("savedZoom")).toDouble(zoom), zoom);
}

QJsonObject runtimeWeatherToJson(const core::WeatherState& weather)
{
    return QJsonObject{
        {QStringLiteral("type"), weather.typeId()},
        {QStringLiteral("intensity"), weather.intensity},
        {QStringLiteral("time"), std::isfinite(weather.elapsed) ? qMax(0.0, weather.elapsed) : 0.0},
        {QStringLiteral("thunderSe"), weather.thunderSePath},
        {QStringLiteral("thunderVolume"), qBound(0, weather.thunderVolume, 100)},
        {QStringLiteral("runtimeOverride"), weather.runtimeOverride}
    };
}

bool runtimeWeatherFromJson(const QJsonObject& json, core::WeatherState* weather)
{
    if (!weather || json.isEmpty()) return false;
    core::WeatherState restored;
    restored.setConfig(json.value(QStringLiteral("type")).toString(QStringLiteral("none")),
                       json.value(QStringLiteral("intensity")).toInt(50),
                       json.value(QStringLiteral("thunderSe")).toString(),
                       json.value(QStringLiteral("thunderVolume")).toInt(90),
                       json.value(QStringLiteral("runtimeOverride")).toBool(true));
    restored.elapsed = finiteNonNegative(json.value(QStringLiteral("time")).toDouble());
    *weather = restored;
    return true;
}

int runtimeSnapshotPayloadVersion(const QJsonObject& snapshot)
{
    if (snapshot.isEmpty()) return 0;
    const int version = snapshot.value(QStringLiteral("runtimeVersion")).toInt(0);
    return qBound(0, version, RuntimeSnapshotPayloadVersion);
}

} // namespace game
