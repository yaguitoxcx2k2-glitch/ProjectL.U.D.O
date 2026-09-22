// ============================================================================
// RuntimePersistence.h — contrato versionado do estado vivo da partida.
//
// SaveFormat continua 3. Este payload interno pode evoluir sem quebrar slots
// antigos: GameSave guarda `runtime`, e este módulo normaliza os campos que
// precisam sobreviver exatamente ao Save/Load (câmera, clima, relógios e rotas).
// ============================================================================
#pragma once

#include "core/Weather.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>

namespace game {

inline constexpr int RuntimeSnapshotPayloadVersion = 4;

struct RuntimeCameraState {
    bool active = false;
    bool moving = false;
    bool tweenCenter = true;
    bool tweenZoom = true;
    bool follow = false;
    bool saved = false;
    QString target;
    QVariantMap params;
    QPointF center;
    QPointF startCenter;
    QPointF endCenter;
    QPointF savedCenter;
    double startZoom = 2.0;
    double endZoom = 2.0;
    double savedZoom = 2.0;
    double elapsed = 0.0;
    double duration = 0.0;
    double followSpeed = 6.0;
    double deadzone = 0.0;
    QString ease = QStringLiteral("smooth");

    QJsonObject toJson(double currentZoom) const;
    /// Restaura também `currentZoom`. Payloads antigos sem estado de tween
    /// continuam válidos e simplesmente retomam uma câmera estável.
    void fromJson(const QJsonObject& json, double* currentZoom);
};

QJsonObject runtimeWeatherToJson(const core::WeatherState& weather);
bool runtimeWeatherFromJson(const QJsonObject& json, core::WeatherState* weather);

/// 0 significa snapshot legado (RC2.13 e anteriores) sem marcador interno.
int runtimeSnapshotPayloadVersion(const QJsonObject& snapshot);

} // namespace game
