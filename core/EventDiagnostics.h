#pragma once

#include "ProjectReferenceIndex.h"
#include "ProjectValidator.h"

#include <QHash>
#include <QJsonArray>

namespace core {

/// Diagnóstico navegável derivado do ProjectValidator. Não representa uma
/// segunda validação: apenas acrescenta frequência, sugestão e localização
/// estruturada para consumidores como o painel Problems.
struct EventDiagnostic {
    ValidationSeverity severity = ValidationSeverity::Info;
    QString code;
    QString location;
    QString message;
    QString suggestion;
    QVector<QString> relatedLocations;
    int frequency = 1;
    bool safelyFixable = false;
    ProjectReferenceLocation target;
};

QVector<EventDiagnostic> collectProjectDiagnostics(const Editor& editor);
QHash<QString,int> diagnosticFrequencyMap(const QVector<EventDiagnostic>& diagnostics);
QVector<EventDiagnostic> diagnosticsByCode(const QVector<EventDiagnostic>& diagnostics,
                                           const QString& code);
QVector<EventDiagnostic> diagnosticsBySeverity(const QVector<EventDiagnostic>& diagnostics,
                                               ValidationSeverity severity);
QString summarizeDiagnostics(const QVector<EventDiagnostic>& diagnostics);
QString diagnosticsToText(const QVector<EventDiagnostic>& diagnostics);
QJsonArray diagnosticsToJson(const QVector<EventDiagnostic>& diagnostics);

} // namespace core
