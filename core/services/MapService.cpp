#include "MapService.h"

#include "core/Editor.h"

namespace core {

MapDoc* MapService::find(const MapId& id) { return m_editor.mapById(id.toString()); }
const MapDoc* MapService::find(const MapId& id) const { return m_editor.mapById(id.toString()); }

bool MapService::renameMap(const MapId& id, const QString& name, QString* error)
{
    MapDoc* map = find(id);
    if (!map) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return false;
    }
    const QString normalized = name.trimmed();
    if (normalized.isEmpty()) {
        if (error) *error = QStringLiteral("O nome do mapa não pode ficar vazio.");
        return false;
    }
    if (map->name == normalized) return true;
    map->name = normalized;
    map->dirty = true;
    m_editor.markDirty();
    emit m_editor.docsChanged();
    return true;
}

bool MapService::resizeMap(const MapId& id, const QSize& tileSize, QString* error)
{
    if (tileSize.width() < 1 || tileSize.height() < 1 ||
        tileSize.width() > 4096 || tileSize.height() > 4096) {
        if (error) *error = QStringLiteral("O tamanho do mapa deve ficar entre 1 e 4096 tiles.");
        return false;
    }
    const int index = m_editor.mapIndexById(id.toString());
    if (index < 0) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return false;
    }

    const int previousIndex = m_editor.activeDocIdx;
    if (previousIndex != index) m_editor.switchDoc(index);
    MapDoc* map = m_editor.doc();
    if (!map) {
        if (previousIndex >= 0 && previousIndex < m_editor.docs.size()) m_editor.switchDoc(previousIndex);
        if (error) *error = QStringLiteral("Não foi possível ativar o mapa.");
        return false;
    }

    const DocSnapshot before = m_editor.snapshotDoc();
    map->map.width = tileSize.width();
    map->map.height = tileSize.height();
    m_editor.resyncLayerGrids();
    m_editor.pushDocHistory(before, QStringLiteral("Redimensionar mapa"));
    emit m_editor.mapChanged();
    emit m_editor.layersChanged();

    if (previousIndex >= 0 && previousIndex < m_editor.docs.size() && previousIndex != index)
        m_editor.switchDoc(previousIndex);
    return true;
}

} // namespace core
