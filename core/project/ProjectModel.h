#pragma once

#include "StableId.h"
#include "core/Editor.h"

#include <QPoint>
#include <QString>
#include <QVector>

namespace core {

/// Fronteira oficial do modelo persistente durante a migração 4.x.
///
/// O armazenamento legado continua temporariamente em Editor para preservar a
/// ABI/API interna do projeto. Código novo deve depender de ProjectModel ou dos
/// Services, nunca do estado volátil de EditorSession. A fachada deliberadamente
/// não expõe seleção, zoom, ferramentas, clipboard ou workspace.
class ProjectModel final
{
public:
    explicit ProjectModel(Editor& editor) : m_editor(&editor) {}

    QString& name();
    const QString& name() const;
    QString& id();
    const QString& id() const;
    QString& path();
    const QString& path() const;

    MapId startMapId() const;
    void setStartMapId(const MapId& id);
    QPoint& startPosition();
    const QPoint& startPosition() const;

    bool isDirty() const;
    void markDirty();
    void markSaved();

    QVector<MapDoc>& maps();
    const QVector<MapDoc>& maps() const;
    MapDoc* map(const MapId& id);
    const MapDoc* map(const MapId& id) const;

    AssetDatabase& assets();
    const AssetDatabase& assets() const;
    QVector<CommonEvent>& commonEvents();
    const QVector<CommonEvent>& commonEvents() const;
    QVector<NoCodePlugin>& plugins();
    const QVector<NoCodePlugin>& plugins() const;
    QVector<CustomDatabaseDefinition>& customDatabases();
    const QVector<CustomDatabaseDefinition>& customDatabases() const;

    Editor& legacyEditor() { return *m_editor; }
    const Editor& legacyEditor() const { return *m_editor; }

private:
    Editor* m_editor = nullptr;
};

} // namespace core
