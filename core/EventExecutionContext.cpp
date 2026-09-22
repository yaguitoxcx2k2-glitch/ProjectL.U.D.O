#include "EventExecutionContext.h"

#include "Editor.h"

namespace core {

QString eventExecutionModeId(EventExecutionMode mode)
{
    switch (mode) {
    case EventExecutionMode::Normal: return QStringLiteral("normal");
    case EventExecutionMode::OnPageActivated: return QStringLiteral("onPageActivated");
    case EventExecutionMode::Autorun: return QStringLiteral("autorun");
    case EventExecutionMode::Parallel: return QStringLiteral("parallel");
    }
    return QStringLiteral("normal");
}

EventExecutionMode eventExecutionModeFromId(const QString& rawId)
{
    const QString id = rawId.trimmed();
    if (id.compare(QLatin1String("onPageActivated"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("page"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("automatic"), Qt::CaseInsensitive) == 0)
        return EventExecutionMode::OnPageActivated;
    if (id.compare(QLatin1String("autorun"), Qt::CaseInsensitive) == 0)
        return EventExecutionMode::Autorun;
    if (id.compare(QLatin1String("parallel"), Qt::CaseInsensitive) == 0)
        return EventExecutionMode::Parallel;
    return EventExecutionMode::Normal;
}

namespace {

bool mapContainsEvent(const Editor& editor, const QString& mapId, const QString& eventId)
{
    const MapDoc* map = mapId.isEmpty() ? editor.doc() : editor.mapById(mapId);
    if (!map) return false;
    for (const MapEvent& event : map->events)
        if (event.id == eventId) return true;
    return false;
}

EventTargetResolution invalidTarget(const QString& requested, EventTargetError error)
{
    EventTargetResolution result;
    result.requested = requested;
    result.error = error;
    return result;
}

} // namespace

EventTargetResolution resolveEventTarget(const QString& rawRequested,
                                         const EventExecutionContext& context,
                                         const Editor* editor,
                                         const EventTargetResolveOptions& options)
{
    const QString requested = rawRequested.trimmed();
    if (requested.isEmpty()) return invalidTarget(requested, EventTargetError::Empty);

    EventTargetResolution result;
    result.requested = requested;

    if (requested == QLatin1String("player")) {
        if (!options.allowPlayer) return invalidTarget(requested, EventTargetError::InvalidSyntax);
        result.kind = EventTargetKind::Player;
        result.error = EventTargetError::None;
        result.canonical = QStringLiteral("player");
        return result;
    }

    if (requested == QLatin1String("self")) {
        if (!options.allowThisEvent) return invalidTarget(requested, EventTargetError::InvalidSyntax);
        if (!context.hasMapEvent()) return invalidTarget(requested, EventTargetError::ThisEventUnavailable);
        result.kind = EventTargetKind::MapEvent;
        result.error = EventTargetError::None;
        result.eventId = context.mapEventId;
        result.canonical = context.thisEventTarget();
        return result;
    }

    if (requested == QLatin1String("position")) {
        if (!options.allowPosition) return invalidTarget(requested, EventTargetError::InvalidSyntax);
        result.kind = EventTargetKind::Position;
        result.error = EventTargetError::None;
        result.canonical = QStringLiteral("position");
        return result;
    }

    if (requested.startsWith(QLatin1String("event:"))) {
        if (!options.allowExplicitEvent) return invalidTarget(requested, EventTargetError::InvalidSyntax);
        const QString eventId = requested.mid(6).trimmed();
        if (eventId.isEmpty() || eventId.contains(QLatin1Char(':')))
            return invalidTarget(requested, EventTargetError::InvalidSyntax);
        if (editor && options.verifyExplicitEvent && !mapContainsEvent(*editor, context.mapId, eventId))
            return invalidTarget(requested, EventTargetError::EventNotFound);
        result.kind = EventTargetKind::MapEvent;
        result.error = EventTargetError::None;
        result.eventId = eventId;
        result.canonical = QStringLiteral("event:") + eventId;
        return result;
    }

    return invalidTarget(requested, EventTargetError::InvalidSyntax);
}

QString eventTargetErrorId(EventTargetError error)
{
    switch (error) {
    case EventTargetError::None: return QStringLiteral("none");
    case EventTargetError::Empty: return QStringLiteral("empty");
    case EventTargetError::ThisEventUnavailable: return QStringLiteral("this-event-unavailable");
    case EventTargetError::InvalidSyntax: return QStringLiteral("invalid-syntax");
    case EventTargetError::EventNotFound: return QStringLiteral("event-not-found");
    }
    return QStringLiteral("invalid-syntax");
}

} // namespace core
