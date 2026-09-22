#pragma once

#include "core/project/StableId.h"

#include <QSize>
#include <QString>

namespace core {
class Editor;
struct MapDoc;

class MapService final
{
public:
    explicit MapService(Editor& editor) : m_editor(editor) {}

    MapDoc* find(const MapId& id);
    const MapDoc* find(const MapId& id) const;
    bool renameMap(const MapId& id, const QString& name, QString* error = nullptr);
    bool resizeMap(const MapId& id, const QSize& tileSize, QString* error = nullptr);

private:
    Editor& m_editor;
};

} // namespace core
