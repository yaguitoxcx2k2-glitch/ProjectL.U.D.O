#include "ProjectDashboard.h"

#include "core/Editor.h"
#include "core/ProjectIO.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace ui {

ProjectDashboardSnapshot ProjectDashboard::inspect(const QString& projectPath, bool includeHealth)
{
    ProjectDashboardSnapshot out;
    const QFileInfo info(projectPath);
    out.path = info.absoluteFilePath();
    out.exists = info.exists();
    out.name = info.completeBaseName();
    if (out.exists) {
        out.fileBytes = info.size();
        out.modified = info.lastModified();
    }
    out.recovery = ProjectRecoveryManager::statusFor(out.path);

    QFile file(out.path);
    if (out.exists && file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            out.projectId = root.value(QStringLiteral("projectId")).toString();
            out.name = root.value(QStringLiteral("projectName")).toString(out.name);
            out.rpgMakerProjectRoot = root.value(QStringLiteral("rpgMakerProjectRoot")).toString();
            out.ludoVersion = root.value(QStringLiteral("editorVersion")).toString();
            if (out.ludoVersion.isEmpty())
                out.ludoVersion = root.value(QStringLiteral("engineVersion")).toString(); // legado
            out.formatVersion = root.value(QStringLiteral("formatVersion")).toInt(1);
            out.targetEngine = root.value(QStringLiteral("targetEngine")).toString();
            if (out.targetEngine.isEmpty()) {
                const QString kind = root.value(QStringLiteral("projectKind")).toString();
                out.targetEngine = kind.contains(QStringLiteral("-mv-")) ? QStringLiteral("mv") : QStringLiteral("mz");
            }
        }
    }

    if (!includeHealth || !out.exists) return out;
    out.healthInspected = true;
    core::Editor staging;
    QString error;
    if (!core::io::loadProject(staging, out.path, &error, core::io::ProjectLoadMode::ReadOnlyPreview)) {
        out.healthError = error;
        return out;
    }
    out.health = core::ProjectHealth::inspect(staging);
    out.healthAvailable = true;
    return out;
}

} // namespace ui
