#pragma once

#include <QJsonObject>
#include <QString>

namespace game {

// Crash/incident context intentionally contains only technical runtime state.
// It does not install signal handlers; callers may serialize this object from
// an existing controlled exception/fatal-error path.
struct RuntimeCrashContext {
    QString rendererBackend;
    QString mapId;
    QString eventId;
    QString commandType;
    int commandIndex = -1;
    double frameMs = 0.0;
    double simulationMs = 0.0;
    double eventsMs = 0.0;
    double renderMs = 0.0;

    QJsonObject toJson() const;
};

} // namespace game
