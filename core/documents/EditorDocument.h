#pragma once

#include "core/project/StableId.h"

#include <QString>

namespace core {
class Editor;

class EditorDocument
{
public:
    virtual ~EditorDocument() = default;
    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual bool isDirty() const = 0;
};

class MapEditorDocument final : public EditorDocument
{
public:
    MapEditorDocument(Editor& editor, MapId mapId) : m_editor(editor), m_mapId(std::move(mapId)) {}

    QString id() const override { return m_mapId.toString(); }
    QString title() const override;
    bool isDirty() const override;
    MapId mapId() const { return m_mapId; }

private:
    Editor& m_editor;
    MapId m_mapId;
};

} // namespace core
