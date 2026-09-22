#pragma once

#include "core/project/ProjectModel.h"

#include <QString>

namespace core {
class Editor;

class ProjectService final
{
public:
    explicit ProjectService(Editor& editor);

    ProjectModel& project() { return m_project; }
    const ProjectModel& project() const { return m_project; }

    bool renameProject(const QString& name, QString* error = nullptr);
    bool setStartPosition(const MapId& mapId, const QPoint& position, QString* error = nullptr);

private:
    Editor& m_editor;
    ProjectModel m_project;
};

} // namespace core
