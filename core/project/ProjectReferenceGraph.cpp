#include "ProjectReferenceGraph.h"

#include "core/Editor.h"

namespace core {

ProjectDependencySnapshot ProjectReferenceGraph::snapshot() const
{
    return ProjectDependencyIndex::build(m_editor);
}

QVector<ProjectReferenceUsage> ProjectReferenceGraph::uses(ReferenceSymbolKind kind, const QString& id) const
{
    return findProjectUses(m_editor, kind, id);
}

bool ProjectReferenceGraph::canDelete(ReferenceSymbolKind kind, const QString& id,
                                      QVector<ProjectReferenceUsage>* blockers) const
{
    const QVector<ProjectReferenceUsage> found = uses(kind, id);
    if (blockers) *blockers = found;
    return found.isEmpty();
}

} // namespace core
