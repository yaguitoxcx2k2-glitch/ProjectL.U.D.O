#include "RuntimeCrashContext.h"

#include "core/Version.h"

#include <QSysInfo>
#include <QtGlobal>

namespace game {

QJsonObject RuntimeCrashContext::toJson() const
{
    QJsonObject timings;
    timings[QStringLiteral("frameMs")] = frameMs;
    timings[QStringLiteral("simulationMs")] = simulationMs;
    timings[QStringLiteral("eventsMs")] = eventsMs;
    timings[QStringLiteral("renderMs")] = renderMs;

    QJsonObject current;
    current[QStringLiteral("mapId")] = mapId;
    current[QStringLiteral("eventId")] = eventId;
    current[QStringLiteral("commandType")] = commandType;
    current[QStringLiteral("commandIndex")] = commandIndex;

    QJsonObject root;
    root[QStringLiteral("schema")] = QStringLiteral("ludo.runtime.crash-context.v1");
    root[QStringLiteral("engineVersion")] = QString::fromLatin1(core::version::Engine);
    root[QStringLiteral("qtVersion")] = QString::fromLatin1(qVersion());
    root[QStringLiteral("osProduct")] = QSysInfo::prettyProductName();
    root[QStringLiteral("cpuArchitecture")] = QSysInfo::currentCpuArchitecture();
    root[QStringLiteral("rendererBackend")] = rendererBackend;
    root[QStringLiteral("current")] = current;
    root[QStringLiteral("timings")] = timings;
    return root;
}

} // namespace game
