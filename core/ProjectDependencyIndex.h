#pragma once

#include "ProjectReferenceIndex.h"

#include <QString>
#include <QVector>

namespace core {
class Editor;

/// Nó unificado do grafo de autoria. Referências semânticas usam IDs estáveis;
/// assets usam os GUIDs do AssetDatabase. Nós auxiliares representam origens
/// que ainda não possuem símbolo próprio no ProjectReferenceIndex.
enum class ProjectDependencyNodeKind {
    Reference,
    Asset,
    Owner
};

struct ProjectDependencyNode {
    ProjectDependencyNodeKind kind = ProjectDependencyNodeKind::Reference;
    QString key;
    QString label;
    QString context;
    QString typeLabel;
    ProjectReferenceLocation location;

    ReferenceSymbolKind referenceKind = ReferenceSymbolKind::Map;
    QString symbolId;
    QString parentId;

    QString assetId;
    QString assetPath;
    bool missing = false;
};

struct ProjectDependencyEdge {
    QString sourceKey;
    QString targetKey;
    QString detail;
    ProjectReferenceLocation sourceLocation;
};

struct ProjectDependencySnapshot {
    QVector<ProjectDependencyNode> nodes;
    QVector<ProjectDependencyEdge> edges;

    const ProjectDependencyNode* node(const QString& key) const;
    QVector<ProjectDependencyEdge> incoming(const QString& key) const;
    QVector<ProjectDependencyEdge> outgoing(const QString& key) const;
    int incomingCount(const QString& key) const;
    int outgoingCount(const QString& key) const;
    int unreferencedAssetCount() const;
};

class ProjectDependencyIndex final
{
public:
    static QString referenceKey(ReferenceSymbolKind kind, const QString& id,
                                const QString& parentId = QString());
    static QString assetKey(const QString& assetId);

    /// Constrói um snapshot somente-leitura do mesmo grafo de referências que
    /// alimenta Find Uses e da mesma lista assetReferences usada pelo documento/exportador de mapas.
    static ProjectDependencySnapshot build(const Editor& editor);
};

} // namespace core
