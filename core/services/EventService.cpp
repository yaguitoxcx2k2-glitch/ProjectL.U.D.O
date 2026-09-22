#include "EventService.h"

#include "core/Editor.h"

namespace core {

MapEvent* EventService::findEvent(const MapId& mapId, const EventId& eventId)
{
    MapDoc* map = m_editor.mapById(mapId.toString());
    if (!map) return nullptr;
    for (MapEvent& event : map->events)
        if (event.id == eventId.toString()) return &event;
    return nullptr;
}

EventId EventService::createEvent(const MapId& mapId, const QPoint& cell, QString* error)
{
    const int index = m_editor.mapIndexById(mapId.toString());
    if (index < 0) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return {};
    }
    const int previousIndex = m_editor.activeDocIdx;
    if (previousIndex != index) m_editor.switchDoc(index);
    const QString created = m_editor.addEvent(cell);
    if (previousIndex >= 0 && previousIndex < m_editor.docs.size() && previousIndex != index)
        m_editor.switchDoc(previousIndex);
    if (created.isEmpty() && error) *error = QStringLiteral("Não foi possível criar o evento nesta célula.");
    return EventId::fromLegacy(created);
}

bool EventService::removeEvent(const MapId& mapId, const EventId& eventId, QString* error)
{
    const int index = m_editor.mapIndexById(mapId.toString());
    if (index < 0 || !findEvent(mapId, eventId)) {
        if (error) *error = QStringLiteral("Evento inexistente.");
        return false;
    }
    const int previousIndex = m_editor.activeDocIdx;
    if (previousIndex != index) m_editor.switchDoc(index);
    m_editor.removeEvent(eventId.toString());
    if (previousIndex >= 0 && previousIndex < m_editor.docs.size() && previousIndex != index)
        m_editor.switchDoc(previousIndex);
    return true;
}

} // namespace core
