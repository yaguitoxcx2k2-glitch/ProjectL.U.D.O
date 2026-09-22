#pragma once

#include "core/ProjectHealth.h"
#include "ProjectRecoveryManager.h"

#include <QDateTime>
#include <QString>

namespace ui {

struct ProjectDashboardSnapshot
{
    QString path;
    QString projectId;
    QString name;
    QString ludoVersion;
    QString targetEngine = QStringLiteral("mz");
    QString rpgMakerProjectRoot;
    int formatVersion = 0;
    bool exists = false;
    qint64 fileBytes = 0;
    QDateTime modified;

    ProjectRecoveryStatus recovery;

    bool healthInspected = false;
    bool healthAvailable = false;
    QString healthError;
    core::ProjectHealthSnapshot health;
};

/// Leitura compartilhada do cartão/detalhes do Project Manager. O modo leve
/// lê apenas metadados + recovery; o modo completo carrega o projeto em um
/// Editor de staging e usa a autoridade ProjectHealth existente.
class ProjectDashboard
{
public:
    static ProjectDashboardSnapshot inspect(const QString& projectPath, bool includeHealth);
};

} // namespace ui
