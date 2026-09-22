#pragma once

#include "core/project/StableId.h"

#include <QPoint>
#include <QString>

namespace core {
class Editor;
struct MapEvent;

class EventService final
{
public:
    explicit EventService(Editor& editor) : m_editor(editor) {}

    EventId createEvent(const MapId& mapId, const QPoint& cell, QString* error = nullptr);
    bool removeEvent(const MapId& mapId, const EventId& eventId, QString* error = nullptr);
    MapEvent* findEvent(const MapId& mapId, const EventId& eventId);

private:
    Editor& m_editor;
};

} // namespace core
