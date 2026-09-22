#include "ProjectHealth.h"

#include <QObject>

namespace core {

ProjectHealthArea ProjectHealth::areaForIssue(const ValidationIssue& issue)
{
    const QString code = issue.code.toLower();
    if (code.startsWith(QStringLiteral("asset.")) ||
        code.startsWith(QStringLiteral("layer.image.")))
        return ProjectHealthArea::Assets;
    if (code.startsWith(QStringLiteral("tileset.")) ||
        code.startsWith(QStringLiteral("autotile.")) ||
        code.startsWith(QStringLiteral("terrain.")) ||
        code.startsWith(QStringLiteral("wang.")))
        return ProjectHealthArea::Tilesets;
    if (code.startsWith(QStringLiteral("map.")) ||
        code.startsWith(QStringLiteral("layer.")))
        return ProjectHealthArea::Maps;
    return ProjectHealthArea::Project;
}

QString projectHealthAreaLabel(ProjectHealthArea area)
{
    switch (area) {
    case ProjectHealthArea::Project: return QObject::tr("Projeto");
    case ProjectHealthArea::Maps: return QObject::tr("Mapas e camadas");
    case ProjectHealthArea::Tilesets: return QObject::tr("Tilesets e autotiles");
    case ProjectHealthArea::Assets: return QObject::tr("Assets de mapa");
    }
    return QObject::tr("Projeto");
}

ProjectHealthSnapshot ProjectHealth::inspect(const Editor& editor)
{
    ProjectHealthSnapshot snapshot;
    snapshot.validation = ProjectValidator::validate(editor);

    const QVector<ProjectHealthArea> ordered = {
        ProjectHealthArea::Project,
        ProjectHealthArea::Maps,
        ProjectHealthArea::Tilesets,
        ProjectHealthArea::Assets
    };
    snapshot.areas.reserve(ordered.size());
    for (ProjectHealthArea area : ordered) snapshot.areas.push_back({area, 0, 0, 0});

    for (const ValidationIssue& issue : snapshot.validation.issues) {
        if (issue.safelyFixable) ++snapshot.safelyFixableCount;
        const ProjectHealthArea area = areaForIssue(issue);
        for (ProjectHealthAreaSummary& summary : snapshot.areas) {
            if (summary.area != area) continue;
            if (issue.severity == ValidationSeverity::Error) ++summary.errorCount;
            else if (issue.severity == ValidationSeverity::Warning) ++summary.warningCount;
            else ++summary.infoCount;
            break;
        }
    }
    return snapshot;
}

} // namespace core
