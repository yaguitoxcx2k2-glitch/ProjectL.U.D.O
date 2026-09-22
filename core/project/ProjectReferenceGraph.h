#pragma once

#include "core/ProjectDependencyIndex.h"

namespace core {
class Editor;

/// API de alto nível sobre os índices de referência já existentes. Mantém uma
/// única fonte de verdade para Find Uses, Safe Delete e validação de referências.
class ProjectReferenceGraph final
{
public:
    explicit ProjectReferenceGraph(const Editor& editor) : m_editor(editor) {}

    ProjectDependencySnapshot snapshot() const;
    QVector<ProjectReferenceUsage> uses(ReferenceSymbolKind kind, const QString& id) const;
    bool canDelete(ReferenceSymbolKind kind, const QString& id,
                   QVector<ProjectReferenceUsage>* blockers = nullptr) const;

private:
    const Editor& m_editor;
};

} // namespace core
