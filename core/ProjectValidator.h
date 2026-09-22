#pragma once

#include "Editor.h"

#include <QStringList>

namespace core {

enum class ValidationSeverity { Error, Warning, Info };

struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Info;
    QString code;
    QString location;
    QString message;
    bool safelyFixable = false;
    QString suggestion;
    QVector<QString> relatedLocations;
    int frequency = 1;
};

struct ProjectValidationResult {
    QVector<ValidationIssue> issues;
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;

    bool hasErrors() const { return errorCount > 0; }
    bool isClean() const { return issues.isEmpty(); }
};

class ProjectValidator
{
public:
    static ProjectValidationResult validate(const Editor& ed);
    /// Corrige somente casos determinísticos que não descartam conteúdo.
    static int repairSafe(Editor& ed, QStringList* changes = nullptr);
};

QString validationSeverityLabel(ValidationSeverity severity);

} // namespace core
