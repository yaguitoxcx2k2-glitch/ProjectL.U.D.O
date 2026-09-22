#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace core {
class Editor;

/// Tipos de referência que pertencem ao documento de autoria do LUDO Map Editor.
/// Eventos, switches, variables, pictures e database pertencem ao RPG Maker de destino.
enum class ReferenceSymbolKind {
    Map,
    Tileset
};

QString referenceSymbolKindId(ReferenceSymbolKind kind);
QString referenceSymbolKindLabel(ReferenceSymbolKind kind);

struct ProjectReferenceSymbol {
    ReferenceSymbolKind kind = ReferenceSymbolKind::Map;
    QString id;
    QString parentId;
    QString name;
    QString qualifiedName;
};

struct ProjectReferenceLocation {
    QString ownerType;      // map, tileset, project, asset
    QString ownerId;
    QString ownerName;
    QString mapId;
    QString detail;
};

struct ProjectReferenceUsage {
    ReferenceSymbolKind kind = ReferenceSymbolKind::Map;
    QString symbolId;
    QString parentId;
    ProjectReferenceLocation location;
};

struct ProjectTextOccurrence {
    ProjectReferenceLocation location;
    QString text;
};

QVector<ProjectReferenceSymbol> projectReferenceSymbols(const Editor& ed);
QVector<ProjectReferenceUsage> projectReferenceUsages(const Editor& ed);
QVector<ProjectTextOccurrence> projectTextOccurrences(const Editor& ed);
QVector<ProjectReferenceUsage> findProjectUses(const Editor& ed, ReferenceSymbolKind kind,
                                               const QString& symbolId);
ProjectReferenceLocation projectDefinitionLocation(const Editor& ed,
                                                    ReferenceSymbolKind kind,
                                                    const QString& symbolId,
                                                    const QString& parentId = QString());
QVector<ProjectReferenceSymbol> unusedSymbols(const Editor& ed);
QVector<ProjectReferenceUsage> orphanedReferences(const Editor& ed);
/// Cada item contém as chaves `kind:id` que formam um ciclo de mapas.
QVector<QStringList> circularReferences(const Editor& ed);

/// Renomeia símbolos de autoria por ID estável.
bool renameProjectSymbol(Editor& ed, ReferenceSymbolKind kind, const QString& symbolId,
                         const QString& newName, QString* error = nullptr);

} // namespace core
