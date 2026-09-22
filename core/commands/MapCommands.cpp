#include "MapCommands.h"

#include "core/Editor.h"
#include "core/services/MapService.h"

namespace core {

RenameMapCommand::RenameMapCommand(Editor& editor, MapId mapId, QString newName)
    : m_editor(editor), m_mapId(std::move(mapId)), m_newName(std::move(newName)) {}

bool RenameMapCommand::execute(QString* error)
{
    MapService service(m_editor);
    MapDoc* map = service.find(m_mapId);
    if (!map) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return false;
    }
    if (!m_captured) {
        m_oldName = map->name;
        m_captured = true;
    }
    return service.renameMap(m_mapId, m_newName, error);
}

void RenameMapCommand::undo()
{
    if (!m_captured) return;
    MapService(m_editor).renameMap(m_mapId, m_oldName, nullptr);
}

ResizeMapCommand::ResizeMapCommand(Editor& editor, MapId mapId, QSize newSize)
    : m_editor(editor), m_mapId(std::move(mapId)), m_newSize(newSize) {}

bool ResizeMapCommand::execute(QString* error)
{
    MapService service(m_editor);
    MapDoc* map = service.find(m_mapId);
    if (!map) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return false;
    }
    if (!m_captured) {
        m_oldSize = QSize(map->map.width, map->map.height);
        m_captured = true;
    }
    return service.resizeMap(m_mapId, m_newSize, error);
}

void ResizeMapCommand::undo()
{
    if (!m_captured) return;
    MapService(m_editor).resizeMap(m_mapId, m_oldSize, nullptr);
}

} // namespace core
