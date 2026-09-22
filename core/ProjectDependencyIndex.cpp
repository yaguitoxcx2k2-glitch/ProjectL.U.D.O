#include "ProjectDependencyIndex.h"

#include "AssetDatabase.h"
#include "AssetWorkflow.h"
#include "Editor.h"
#include "ProjectIO.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>

namespace core {
namespace {

ProjectReferenceLocation locationForSymbol(const ProjectReferenceSymbol& symbol)
{
    ProjectReferenceLocation location;
    location.ownerId = symbol.id;
    location.ownerName = symbol.qualifiedName.isEmpty() ? symbol.name : symbol.qualifiedName;
    location.detail = referenceSymbolKindId(symbol.kind);
    if (symbol.kind == ReferenceSymbolKind::Map) {
        location.ownerType = QStringLiteral("map");
        location.mapId = symbol.id;
    } else {
        location.ownerType = QStringLiteral("tileset");
    }
    return location;
}

QString keyForLocation(const ProjectReferenceLocation& location)
{
    if (location.ownerType == QLatin1String("map"))
        return ProjectDependencyIndex::referenceKey(
            ReferenceSymbolKind::Map,
            location.mapId.isEmpty() ? location.ownerId : location.mapId);
    if (location.ownerType == QLatin1String("tileset"))
        return ProjectDependencyIndex::referenceKey(ReferenceSymbolKind::Tileset,
                                                     location.ownerId);
    return QStringLiteral("owner:%1:%2:%3")
        .arg(location.ownerType, location.mapId, location.ownerId);
}

QString targetKey(const ProjectReferenceUsage& usage)
{
    return ProjectDependencyIndex::referenceKey(usage.kind, usage.symbolId, usage.parentId);
}

QString ownerLabel(const ProjectReferenceLocation& location)
{
    if (!location.ownerName.trimmed().isEmpty()) return location.ownerName;
    if (!location.ownerId.trimmed().isEmpty()) return location.ownerId;
    if (!location.ownerType.trimmed().isEmpty()) return location.ownerType;
    return QStringLiteral("Projeto");
}

void ensureOwnerNode(ProjectDependencySnapshot& snapshot, QHash<QString, int>& nodeIndex,
                     const QString& key, const ProjectReferenceLocation& location)
{
    if (key.isEmpty() || nodeIndex.contains(key)) return;
    ProjectDependencyNode node;
    node.kind = ProjectDependencyNodeKind::Owner;
    node.key = key;
    node.label = ownerLabel(location);
    node.context = location.detail;
    node.typeLabel = QObject::tr("Origem");
    node.location = location;
    nodeIndex.insert(key, snapshot.nodes.size());
    snapshot.nodes.push_back(node);
}

} // namespace

QString ProjectDependencyIndex::referenceKey(ReferenceSymbolKind kind, const QString& id,
                                             const QString& parentId)
{
    return QStringLiteral("ref:%1:%2:%3")
        .arg(referenceSymbolKindId(kind), parentId, id);
}

QString ProjectDependencyIndex::assetKey(const QString& assetId)
{
    return QStringLiteral("asset:%1").arg(assetId);
}

const ProjectDependencyNode* ProjectDependencySnapshot::node(const QString& key) const
{
    for (const ProjectDependencyNode& candidate : nodes)
        if (candidate.key == key) return &candidate;
    return nullptr;
}

QVector<ProjectDependencyEdge> ProjectDependencySnapshot::incoming(const QString& key) const
{
    QVector<ProjectDependencyEdge> result;
    for (const ProjectDependencyEdge& edge : edges)
        if (edge.targetKey == key) result.push_back(edge);
    return result;
}

QVector<ProjectDependencyEdge> ProjectDependencySnapshot::outgoing(const QString& key) const
{
    QVector<ProjectDependencyEdge> result;
    for (const ProjectDependencyEdge& edge : edges)
        if (edge.sourceKey == key) result.push_back(edge);
    return result;
}

int ProjectDependencySnapshot::incomingCount(const QString& key) const
{
    int count = 0;
    for (const ProjectDependencyEdge& edge : edges) if (edge.targetKey == key) ++count;
    return count;
}

int ProjectDependencySnapshot::outgoingCount(const QString& key) const
{
    int count = 0;
    for (const ProjectDependencyEdge& edge : edges) if (edge.sourceKey == key) ++count;
    return count;
}

int ProjectDependencySnapshot::unreferencedAssetCount() const
{
    int count = 0;
    for (const ProjectDependencyNode& node : nodes)
        if (node.kind == ProjectDependencyNodeKind::Asset && !node.missing && incomingCount(node.key) == 0)
            ++count;
    return count;
}

ProjectDependencySnapshot ProjectDependencyIndex::build(const Editor& editor)
{
    ProjectDependencySnapshot snapshot;
    QHash<QString, int> nodeIndex;

    const QVector<ProjectReferenceSymbol> symbols = projectReferenceSymbols(editor);
    snapshot.nodes.reserve(symbols.size() + editor.assetDatabase.records().size());
    for (const ProjectReferenceSymbol& symbol : symbols) {
        ProjectDependencyNode node;
        node.kind = ProjectDependencyNodeKind::Reference;
        node.key = referenceKey(symbol.kind, symbol.id, symbol.parentId);
        node.label = symbol.name.trimmed().isEmpty() ? symbol.qualifiedName : symbol.name;
        node.context = symbol.qualifiedName;
        if (node.context == node.label) node.context.clear();
        node.typeLabel = referenceSymbolKindLabel(symbol.kind);
        node.location = locationForSymbol(symbol);
        node.referenceKind = symbol.kind;
        node.symbolId = symbol.id;
        node.parentId = symbol.parentId;
        nodeIndex.insert(node.key, snapshot.nodes.size());
        snapshot.nodes.push_back(node);
    }

    QSet<QString> mapAssetCategories;
    for (const AssetCategoryInfo& category : AssetWorkflow::categories())
        mapAssetCategories.insert(category.id.toLower());

    for (const AssetRecord& record : editor.assetDatabase.records()) {
        const QString category = record.category.isEmpty()
                                     ? AssetWorkflow::categoryIdForPath(record.path)
                                     : record.category;
        if (record.type.compare(QStringLiteral("image"), Qt::CaseInsensitive) != 0 ||
            !mapAssetCategories.contains(category.toLower()))
            continue;
        ProjectDependencyNode node;
        node.kind = ProjectDependencyNodeKind::Asset;
        node.key = assetKey(record.id);
        node.label = record.path.section(QLatin1Char('/'), -1);
        if (node.label.isEmpty()) node.label = record.path;
        node.context = record.path;
        node.typeLabel = QObject::tr("Arquivo");
        node.assetId = record.id;
        node.assetPath = record.path;
        node.missing = record.missing;
        node.location.ownerType = QStringLiteral("asset");
        node.location.ownerId = record.id;
        node.location.ownerName = node.label;
        node.location.detail = record.path;
        nodeIndex.insert(node.key, snapshot.nodes.size());
        snapshot.nodes.push_back(node);
    }

    QSet<QString> uniqueEdges;
    for (const ProjectReferenceUsage& usage : projectReferenceUsages(editor)) {
        const QString source = keyForLocation(usage.location);
        const QString target = targetKey(usage);
        if (source.isEmpty() || target.isEmpty()) continue;
        ensureOwnerNode(snapshot, nodeIndex, source, usage.location);
        if (!nodeIndex.contains(target)) continue;
        const QString unique = source + QLatin1Char('|') + target + QLatin1Char('|') +
                               usage.location.detail;
        if (uniqueEdges.contains(unique)) continue;
        uniqueEdges.insert(unique);
        snapshot.edges.push_back({source, target, usage.location.detail, usage.location});
    }

    // Assets usam o grafo assetReferences do próprio documento Map Only.
    // Não existe mais um segundo payload de runtime dentro do Editor Desktop.
    const QJsonObject payload = io::buildProjectPayload(editor);
    ProjectReferenceLocation projectLocation;
    projectLocation.ownerType = QStringLiteral("project");
    projectLocation.ownerName = QObject::tr("Projeto de mapas");
    const QString projectKey = QStringLiteral("owner:project:assets");
    for (const QJsonValue& value : payload.value(QStringLiteral("assetReferences")).toArray()) {
        const QJsonObject ref = value.toObject();
        const QString id = ref.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) continue;
        const QString target = assetKey(id);
        if (!nodeIndex.contains(target)) continue;
        ProjectReferenceLocation location = projectLocation;
        location.detail = ref.value(QStringLiteral("owner")).toString();
        ensureOwnerNode(snapshot, nodeIndex, projectKey, projectLocation);
        const QString unique = projectKey + QLatin1Char('|') + target + QLatin1Char('|') + location.detail;
        if (uniqueEdges.contains(unique)) continue;
        uniqueEdges.insert(unique);
        snapshot.edges.push_back({projectKey, target, location.detail, location});
    }

    return snapshot;
}

} // namespace core
