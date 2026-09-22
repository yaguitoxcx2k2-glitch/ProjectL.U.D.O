#include "ProjectModel.h"

#include "core/Editor.h"

namespace core {

QString& ProjectModel::name() { return m_editor->projectName; }
const QString& ProjectModel::name() const { return m_editor->projectName; }
QString& ProjectModel::id() { return m_editor->projectId; }
const QString& ProjectModel::id() const { return m_editor->projectId; }
QString& ProjectModel::path() { return m_editor->projectPath; }
const QString& ProjectModel::path() const { return m_editor->projectPath; }
MapId ProjectModel::startMapId() const { return MapId::fromLegacy(m_editor->startMapId); }
void ProjectModel::setStartMapId(const MapId& id) { m_editor->startMapId = id.toString(); }
QPoint& ProjectModel::startPosition() { return m_editor->startPosition; }
const QPoint& ProjectModel::startPosition() const { return m_editor->startPosition; }
bool ProjectModel::isDirty() const { return m_editor->projectDirty; }
void ProjectModel::markDirty() { m_editor->markDirty(); }
void ProjectModel::markSaved() { m_editor->markSaved(); }
QVector<MapDoc>& ProjectModel::maps() { return m_editor->docs; }
const QVector<MapDoc>& ProjectModel::maps() const { return m_editor->docs; }
MapDoc* ProjectModel::map(const MapId& id) { return m_editor->mapById(id.toString()); }
const MapDoc* ProjectModel::map(const MapId& id) const { return m_editor->mapById(id.toString()); }
AssetDatabase& ProjectModel::assets() { return m_editor->assetDatabase; }
const AssetDatabase& ProjectModel::assets() const { return m_editor->assetDatabase; }
QVector<CommonEvent>& ProjectModel::commonEvents() { return m_editor->commonEvents; }
const QVector<CommonEvent>& ProjectModel::commonEvents() const { return m_editor->commonEvents; }
QVector<NoCodePlugin>& ProjectModel::plugins() { return m_editor->plugins; }
const QVector<NoCodePlugin>& ProjectModel::plugins() const { return m_editor->plugins; }
QVector<CustomDatabaseDefinition>& ProjectModel::customDatabases() { return m_editor->customDatabases; }
const QVector<CustomDatabaseDefinition>& ProjectModel::customDatabases() const { return m_editor->customDatabases; }

} // namespace core
