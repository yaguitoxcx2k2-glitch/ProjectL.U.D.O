#include "ProjectService.h"

#include "core/Editor.h"

namespace core {

ProjectService::ProjectService(Editor& editor) : m_editor(editor), m_project(editor) {}

bool ProjectService::renameProject(const QString& name, QString* error)
{
    const QString normalized = name.trimmed();
    if (normalized.isEmpty()) {
        if (error) *error = QStringLiteral("O nome do projeto não pode ficar vazio.");
        return false;
    }
    if (m_project.name() == normalized) return true;
    m_project.name() = normalized;
    m_project.markDirty();
    return true;
}

bool ProjectService::setStartPosition(const MapId& mapId, const QPoint& position, QString* error)
{
    MapDoc* map = m_project.map(mapId);
    if (!map) {
        if (error) *error = QStringLiteral("Mapa inicial inexistente.");
        return false;
    }
    const QPoint safe(qBound(0, position.x(), qMax(0, map->map.width - 1)),
                      qBound(0, position.y(), qMax(0, map->map.height - 1)));
    m_project.setStartMapId(mapId);
    m_project.startPosition() = safe;
    m_project.markDirty();
    return true;
}

} // namespace core
