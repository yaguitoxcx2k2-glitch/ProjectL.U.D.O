#include "EditorDocument.h"

#include "core/Editor.h"

namespace core {

QString MapEditorDocument::title() const
{
    const MapDoc* map = m_editor.mapById(m_mapId.toString());
    return map ? map->name : QString();
}

bool MapEditorDocument::isDirty() const
{
    const MapDoc* map = m_editor.mapById(m_mapId.toString());
    return map && map->dirty;
}

} // namespace core
