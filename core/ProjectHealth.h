#pragma once

#include "ProjectValidator.h"

#include <QVector>

namespace core {

enum class ProjectHealthArea {
    Project,
    Maps,
    Tilesets,
    Assets
};

struct ProjectHealthAreaSummary {
    ProjectHealthArea area = ProjectHealthArea::Project;
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
};

struct ProjectHealthSnapshot {
    ProjectValidationResult validation;
    QVector<ProjectHealthAreaSummary> areas;
    int safelyFixableCount = 0;

    bool readyForExport() const { return !validation.hasErrors(); }
    bool isClean() const { return validation.isClean(); }
};

class ProjectHealth
{
public:
    static ProjectHealthSnapshot inspect(const Editor& editor);
    static ProjectHealthArea areaForIssue(const ValidationIssue& issue);
};

QString projectHealthAreaLabel(ProjectHealthArea area);

} // namespace core
