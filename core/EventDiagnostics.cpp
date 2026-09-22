#include "EventDiagnostics.h"

#include "Editor.h"

#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>

namespace core {
namespace {

QString defaultSuggestion(const ValidationIssue& issue)
{
    if (!issue.suggestion.trimmed().isEmpty()) return issue.suggestion;
    if (issue.safelyFixable) return QObject::tr("Use Corrigir para aplicar a normalização segura.");
    if (issue.severity == ValidationSeverity::Error) return QObject::tr("Abra o comando ou recurso indicado e corrija a referência antes de exportar.");
    if (issue.severity == ValidationSeverity::Warning) return QObject::tr("Revise este ponto para evitar comportamento dependente de contexto.");
    return QObject::tr("Informação de compatibilidade; nenhuma ação obrigatória.");
}

ProjectReferenceLocation resolveTarget(const Editor& ed, const ValidationIssue& issue)
{
    ProjectReferenceLocation target;
    target.ownerName = issue.location;
    target.detail = issue.code;
    for (const MapDoc& map : ed.docs) {
        const QString mapPrefix=QObject::tr("Mapa “%1”").arg(map.name);
        if (!issue.location.startsWith(mapPrefix)) continue;
        target.mapId=map.id;target.ownerType=QStringLiteral("map");target.ownerId=map.id;
        for (const MapEvent& event : map.events) {
            const QString eventPrefix=QObject::tr("%1 / evento “%2”").arg(mapPrefix,event.name);
            if (!issue.location.startsWith(eventPrefix)) continue;
            target.ownerType=QStringLiteral("mapEvent");target.ownerId=event.id;target.ownerName=event.name;
            const QRegularExpressionMatch page=QRegularExpression(QObject::tr("/ página (\\d+)")).match(issue.location);
            const QRegularExpressionMatch command=QRegularExpression(QObject::tr("/ comando (\\d+)")).match(issue.location);
            if(page.hasMatch())target.pageIndex=page.captured(1).toInt()-1;
            if(command.hasMatch())target.commandIndex=command.captured(1).toInt()-1;
            return target;
        }
        return target;
    }
    for (const CommonEvent& common : ed.commonEvents) {
        const QString prefix=QObject::tr("Evento comum “%1”").arg(common.name);
        if(!issue.location.startsWith(prefix))continue;
        target.ownerType=QStringLiteral("commonEvent");target.ownerId=common.id;target.ownerName=common.name;
        const QRegularExpressionMatch command=QRegularExpression(QObject::tr("/ comando (\\d+)")).match(issue.location);
        if(command.hasMatch())target.commandIndex=command.captured(1).toInt()-1;
        return target;
    }
    target.ownerType=QStringLiteral("project");target.ownerId=QStringLiteral("validation");
    return target;
}
}

QHash<QString,int> diagnosticFrequencyMap(const QVector<EventDiagnostic>& diagnostics)
{
    QHash<QString,int> out;for(const EventDiagnostic& diagnostic:diagnostics)++out[diagnostic.code];return out;
}

QVector<EventDiagnostic> collectProjectDiagnostics(const Editor& editor)
{
    const ProjectValidationResult validation=ProjectValidator::validate(editor);
    QVector<EventDiagnostic> out;out.reserve(validation.issues.size());
    for(const ValidationIssue& issue:validation.issues){EventDiagnostic diagnostic;diagnostic.severity=issue.severity;diagnostic.code=issue.code;diagnostic.location=issue.location;diagnostic.message=issue.message;diagnostic.suggestion=defaultSuggestion(issue);diagnostic.relatedLocations=issue.relatedLocations;diagnostic.safelyFixable=issue.safelyFixable;diagnostic.target=resolveTarget(editor,issue);out.push_back(diagnostic);}
    const QHash<QString,int> frequencies=diagnosticFrequencyMap(out);for(EventDiagnostic& diagnostic:out)diagnostic.frequency=frequencies.value(diagnostic.code,1);return out;
}

QVector<EventDiagnostic> diagnosticsByCode(const QVector<EventDiagnostic>& diagnostics,const QString& code)
{QVector<EventDiagnostic> out;for(const EventDiagnostic& diagnostic:diagnostics)if(diagnostic.code==code)out.push_back(diagnostic);return out;}

QVector<EventDiagnostic> diagnosticsBySeverity(const QVector<EventDiagnostic>& diagnostics,ValidationSeverity severity)
{QVector<EventDiagnostic> out;for(const EventDiagnostic& diagnostic:diagnostics)if(diagnostic.severity==severity)out.push_back(diagnostic);return out;}

QString summarizeDiagnostics(const QVector<EventDiagnostic>& diagnostics)
{int errors=0,warnings=0,infos=0;for(const EventDiagnostic& diagnostic:diagnostics){if(diagnostic.severity==ValidationSeverity::Error)++errors;else if(diagnostic.severity==ValidationSeverity::Warning)++warnings;else ++infos;}return QObject::tr("%1 erros · %2 avisos · %3 informações").arg(errors).arg(warnings).arg(infos);}

QString diagnosticsToText(const QVector<EventDiagnostic>& diagnostics)
{QStringList lines;lines<<summarizeDiagnostics(diagnostics);for(const EventDiagnostic& d:diagnostics)lines<<QStringLiteral("[%1] %2 · %3\n%4\nSugestão: %5").arg(validationSeverityLabel(d.severity)).arg(d.code).arg(d.location).arg(d.message).arg(d.suggestion);return lines.join(QStringLiteral("\n\n"));}

QJsonArray diagnosticsToJson(const QVector<EventDiagnostic>& diagnostics)
{QJsonArray out;for(const EventDiagnostic& d:diagnostics){QJsonArray related;for(const QString& location:d.relatedLocations)related.append(location);out.append(QJsonObject{{"severity",validationSeverityLabel(d.severity)},{"code",d.code},{"location",d.location},{"message",d.message},{"suggestion",d.suggestion},{"relatedLocations",related},{"frequency",d.frequency},{"safelyFixable",d.safelyFixable},{"mapId",d.target.mapId},{"ownerType",d.target.ownerType},{"ownerId",d.target.ownerId},{"pageIndex",d.target.pageIndex},{"commandIndex",d.target.commandIndex}});}return out;}

} // namespace core
