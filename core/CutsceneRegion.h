#pragma once

#include "Model.h"

#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>

namespace core {

enum class CutsceneRegionOneShotScope { MapVisit, SaveGame };

/// Região autoral do mapa que agenda um Evento Comum ao cruzar sua borda.
/// A execução continua pertencendo ao scheduler/Interpreter existentes.
struct CutsceneRegion {
    QString id = idGen();
    QString name = QStringLiteral("Cutscene");
    QRect tileArea{0, 0, 1, 1};
    QString commonEventId;
    bool triggerOnEnter = true;
    bool triggerOnExit = false;
    bool oneShot = false;
    CutsceneRegionOneShotScope oneShotScope = CutsceneRegionOneShotScope::MapVisit;
    bool enabled = true;
};

inline QString cutsceneRegionScopeId(CutsceneRegionOneShotScope scope)
{
    return scope == CutsceneRegionOneShotScope::SaveGame
        ? QStringLiteral("saveGame") : QStringLiteral("mapVisit");
}

inline CutsceneRegionOneShotScope cutsceneRegionScopeFromId(const QString& id)
{
    return id == QLatin1String("saveGame")
        ? CutsceneRegionOneShotScope::SaveGame : CutsceneRegionOneShotScope::MapVisit;
}

inline QJsonObject cutsceneRegionToJson(const CutsceneRegion& region)
{
    return QJsonObject{{"id", region.id}, {"name", region.name},
        {"x", region.tileArea.x()}, {"y", region.tileArea.y()},
        {"width", region.tileArea.width()}, {"height", region.tileArea.height()},
        {"commonEventId", region.commonEventId},
        {"triggerOnEnter", region.triggerOnEnter}, {"triggerOnExit", region.triggerOnExit},
        {"oneShot", region.oneShot}, {"oneShotScope", cutsceneRegionScopeId(region.oneShotScope)},
        {"enabled", region.enabled}};
}

inline CutsceneRegion cutsceneRegionFromJson(const QJsonObject& object)
{
    CutsceneRegion region;
    region.id = object.value("id").toString(region.id).trimmed();
    if (region.id.isEmpty()) region.id = idGen();
    region.name = object.value("name").toString(QStringLiteral("Cutscene"));
    region.tileArea = QRect(object.value("x").toInt(), object.value("y").toInt(),
                            qMax(1, object.value("width").toInt(1)),
                            qMax(1, object.value("height").toInt(1)));
    region.commonEventId = object.value("commonEventId").toString().trimmed();
    region.triggerOnEnter = object.value("triggerOnEnter").toBool(true);
    region.triggerOnExit = object.value("triggerOnExit").toBool(false);
    region.oneShot = object.value("oneShot").toBool(false);
    region.oneShotScope = cutsceneRegionScopeFromId(object.value("oneShotScope").toString());
    region.enabled = object.value("enabled").toBool(true);
    return region;
}

} // namespace core
