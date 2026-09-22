#include "MapWorkflow.h"
#include "ProjectReferenceIndex.h"

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace core::mapworkflow {
namespace {

int mapIndex(const Editor& editor, const QString& id)
{
    return editor.mapIndexById(id);
}

bool isDescendantOf(const Editor& editor, const QString& candidateId, const QString& ancestorId)
{
    if (candidateId.isEmpty() || ancestorId.isEmpty()) return false;
    QSet<QString> seen;
    QString current = candidateId;
    while (!current.isEmpty() && !seen.contains(current)) {
        if (current == ancestorId) return true;
        seen.insert(current);
        const MapDoc* map = editor.mapById(current);
        if (!map) return false;
        current = map->parentId;
    }
    return false;
}

QVector<QString> childIds(const Editor& editor, const QString& parentId)
{
    QVector<QString> ids;
    for (const MapDoc& map : editor.docs)
        if (map.parentId == parentId) ids.push_back(map.id);
    return ids;
}

QSet<QString> subtreeIds(const Editor& editor, const QString& rootId)
{
    QSet<QString> out;
    std::function<void(const QString&)> visit = [&](const QString& id) {
        if (id.isEmpty() || out.contains(id) || editor.mapIndexById(id) < 0) return;
        out.insert(id);
        for (const MapDoc& map : editor.docs)
            if (map.parentId == id) visit(map.id);
    };
    visit(rootId);
    return out;
}

QVector<MapDoc> flattened(const Editor& editor,
                          const QHash<QString, QVector<QString>>& children,
                          const QVector<QString>& roots)
{
    QVector<MapDoc> out;
    out.reserve(editor.docs.size());
    QSet<QString> emitted;
    std::function<void(const QString&)> append = [&](const QString& id) {
        if (id.isEmpty() || emitted.contains(id)) return;
        const MapDoc* map = editor.mapById(id);
        if (!map) return;
        emitted.insert(id);
        out.push_back(*map);
        for (const QString& child : children.value(id)) append(child);
    };
    for (const QString& root : roots) append(root);
    for (const MapDoc& map : editor.docs) append(map.id);
    return out;
}

QVector<MapDoc> canonicalMaps(const Editor& editor)
{
    QHash<QString, QVector<QString>> children;
    QVector<QString> roots;
    for (const MapDoc& map : editor.docs) {
        if (map.parentId.isEmpty() || editor.mapIndexById(map.parentId) < 0 || map.parentId == map.id)
            roots.push_back(map.id);
        else
            children[map.parentId].push_back(map.id);
    }
    return flattened(editor, children, roots);
}

void restoreActive(Editor& editor, const QString& activeId)
{
    editor.activeDocIdx = 0;
    const int idx = editor.mapIndexById(activeId);
    if (idx >= 0) editor.activeDocIdx = idx;
    if (const LayerPtr active = editor.activeLayer()) editor.session.selectedLayerId = active->id;
    else editor.session.selectedLayerId.clear();
}

void collectLayerIdMap(const LayerPtr& before, const LayerPtr& after,
                       QHash<QString, QString>& layerIds,
                       QHash<QString, QString>& objectIds)
{
    if (!before || !after) return;
    layerIds.insert(before->id, after->id);
    const int objectCount = qMin(before->objects.size(), after->objects.size());
    for (int i = 0; i < objectCount; ++i)
        objectIds.insert(before->objects[i].id, after->objects[i].id);
    const int count = qMin(before->children.size(), after->children.size());
    for (int i = 0; i < count; ++i)
        collectLayerIdMap(before->children[i], after->children[i], layerIds, objectIds);
}

QVariant rewriteVariant(const QVariant& value, const QString& key,
                        const QHash<QString, QString>& mapIds,
                        const QHash<QString, QString>& layerIds,
                        const QHash<QString, QString>& objectIds)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap out;
        const QVariantMap in = value.toMap();
        for (auto it = in.cbegin(); it != in.cend(); ++it)
            out.insert(it.key(), rewriteVariant(it.value(), it.key(), mapIds, layerIds, objectIds));
        return out;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList out;
        for (const QVariant& item : value.toList())
            out.push_back(rewriteVariant(item, QString(), mapIds, layerIds, objectIds));
        return out;
    }
    if (value.typeId() != QMetaType::QString) return value;

    const QString text = value.toString();
    if (mapIds.contains(text)) return mapIds.value(text);
    if (layerIds.contains(text)) return layerIds.value(text);
    if (objectIds.contains(text)) return objectIds.value(text);

    const QString folded = key.toCaseFolded();
    if ((folded == QLatin1String("mapid") || folded.endsWith(QLatin1String("mapid"))) && mapIds.contains(text))
        return mapIds.value(text);
    if ((folded == QLatin1String("layerid") || folded.endsWith(QLatin1String("layerid"))) && layerIds.contains(text))
        return layerIds.value(text);
    if ((folded == QLatin1String("objectid") || folded.endsWith(QLatin1String("objectid"))) && objectIds.contains(text))
        return objectIds.value(text);
    return value;
}

void rewriteLayerProperties(const LayerPtr& layer,
                            const QHash<QString, QString>& mapIds,
                            const QHash<QString, QString>& layerIds,
                            const QHash<QString, QString>& objectIds)
{
    if (!layer) return;
    for (MapObject& object : layer->objects) {
        QHash<QString, QString> rewritten;
        for (auto it = object.properties.cbegin(); it != object.properties.cend(); ++it)
            rewritten.insert(it.key(),
                             rewriteVariant(it.value(), it.key(), mapIds, layerIds, objectIds).toString());
        object.properties = rewritten;
    }
    for (const LayerPtr& child : layer->children)
        rewriteLayerProperties(child, mapIds, layerIds, objectIds);
}

int countExternalReferences(const Editor& editor, const QSet<QString>& deleting)
{
    int count = 0;
    for (const QString& mapId : deleting) {
        const QVector<ProjectReferenceUsage> uses =
            findProjectUses(editor, ReferenceSymbolKind::Map, mapId);
        for (const ProjectReferenceUsage& use : uses) {
            if (!use.location.mapId.isEmpty() && deleting.contains(use.location.mapId)) continue;
            ++count;
        }
    }
    return count;
}

QString uniqueCopyName(const Editor& editor, const QString& original, QSet<QString>& reserved)
{
    QString base = original + QStringLiteral(" - Cópia");
    QString candidate = base;
    int suffix = 2;
    auto exists = [&](const QString& name) {
        if (reserved.contains(name)) return true;
        for (const MapDoc& map : editor.docs)
            if (map.name == name) return true;
        return false;
    };
    while (exists(candidate)) candidate = QStringLiteral("%1 %2").arg(base).arg(suffix++);
    reserved.insert(candidate);
    return candidate;
}

} // namespace

bool canReparent(const Editor& editor, const QString& mapId, const QString& newParentId,
                 QString* error)
{
    if (mapIndex(editor, mapId) < 0) {
        if (error) *error = QStringLiteral("Mapa de origem inexistente.");
        return false;
    }
    if (newParentId.isEmpty()) return true;
    if (mapIndex(editor, newParentId) < 0) {
        if (error) *error = QStringLiteral("Mapa pai inexistente.");
        return false;
    }
    if (newParentId == mapId || isDescendantOf(editor, newParentId, mapId)) {
        if (error) *error = QStringLiteral("A operação criaria um ciclo na árvore de mapas.");
        return false;
    }
    return true;
}

bool reparent(Editor& editor, const QString& mapId, const QString& newParentId, QString* error)
{
    if (!canReparent(editor, mapId, newParentId, error)) return false;
    MapDoc* map = editor.mapById(mapId);
    if (!map) return false;
    if (map->parentId == newParentId) return true;
    map->parentId = newParentId;
    map->dirty = true;
    normalizeHierarchy(editor);
    editor.markDirty();
    return true;
}

bool applyTreeOrder(Editor& editor, const QVector<MapTreePlacement>& placements, QString* error)
{
    if (placements.size() != editor.docs.size()) {
        if (error) *error = QStringLiteral("A árvore não contém todos os mapas do projeto.");
        return false;
    }

    QSet<QString> ids;
    QHash<QString, QString> parents;
    for (const MapTreePlacement& placement : placements) {
        if (placement.mapId.isEmpty() || editor.mapIndexById(placement.mapId) < 0 ||
            ids.contains(placement.mapId)) {
            if (error) *error = QStringLiteral("A árvore contém mapa ausente ou ID duplicado.");
            return false;
        }
        ids.insert(placement.mapId);
        parents.insert(placement.mapId, placement.parentId);
    }

    for (const MapTreePlacement& placement : placements) {
        if (!placement.parentId.isEmpty() && !ids.contains(placement.parentId)) {
            if (error) *error = QStringLiteral("A árvore contém um mapa pai inexistente.");
            return false;
        }
        if (placement.parentId == placement.mapId) {
            if (error) *error = QStringLiteral("Um mapa não pode ser pai de si mesmo.");
            return false;
        }

        QSet<QString> chain;
        QString current = placement.mapId;
        while (!current.isEmpty()) {
            if (chain.contains(current)) {
                if (error) *error = QStringLiteral("A operação criaria um ciclo na árvore de mapas.");
                return false;
            }
            chain.insert(current);
            current = parents.value(current);
        }
    }

    const QString activeId = editor.doc() ? editor.doc()->id : QString();
    QVector<MapDoc> ordered;
    ordered.reserve(editor.docs.size());
    bool changed = false;
    for (int i = 0; i < placements.size(); ++i) {
        const MapTreePlacement& placement = placements[i];
        const MapDoc* source = editor.mapById(placement.mapId);
        if (!source) return false;
        MapDoc copy = *source;
        if (copy.parentId != placement.parentId || editor.docs[i].id != placement.mapId) changed = true;
        if (copy.parentId != placement.parentId) copy.dirty = true;
        copy.parentId = placement.parentId;
        ordered.push_back(copy);
    }

    if (!changed) return true;
    editor.docs = ordered;
    restoreActive(editor, activeId);
    editor.markDirty();
    return true;
}

bool moveSibling(Editor& editor, const QString& mapId, int delta, QString* error)
{
    if (delta == 0) return true;
    const MapDoc* map = editor.mapById(mapId);
    if (!map) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return false;
    }
    if (!map->variationBaseId.isEmpty()) {
        if (error) *error = QStringLiteral("Variações acompanham o mapa-base na árvore.");
        return false;
    }

    const QString activeId = editor.doc() ? editor.doc()->id : QString();
    const QString parentId = map->parentId;
    QVector<QString> siblings;
    for (const MapDoc& item : editor.docs)
        if (item.variationBaseId.isEmpty() && item.parentId == parentId)
            siblings.push_back(item.id);
    const int from = siblings.indexOf(mapId);
    if (from < 0) return false;
    const int to = qBound(0, from + (delta < 0 ? -1 : 1), siblings.size() - 1);
    if (to == from) return false;
    siblings.move(from, to);

    QHash<QString, QVector<QString>> children;
    QVector<QString> roots;
    for (const MapDoc& item : editor.docs) {
        if (!item.variationBaseId.isEmpty()) continue;
        if (item.parentId.isEmpty() || editor.mapIndexById(item.parentId) < 0 || item.parentId == item.id)
            roots.push_back(item.id);
        else
            children[item.parentId].push_back(item.id);
    }
    if (parentId.isEmpty()) roots = siblings;
    else children[parentId] = siblings;

    QVector<MapDoc> ordered;
    ordered.reserve(editor.docs.size());
    QSet<QString> emitted;
    std::function<void(const QString&)> append = [&](const QString& id) {
        if (id.isEmpty() || emitted.contains(id)) return;
        const MapDoc* base = editor.mapById(id);
        if (!base) return;
        emitted.insert(id);
        ordered.push_back(*base);
        // Variações ficam coladas ao cenário-base sem participarem da relação
        // parent/child normal.
        for (const MapDoc& candidate : editor.docs) {
            if (candidate.variationBaseId != id || emitted.contains(candidate.id)) continue;
            emitted.insert(candidate.id);
            ordered.push_back(candidate);
        }
        for (const QString& child : children.value(id)) append(child);
    };
    for (const QString& root : roots) append(root);
    for (const MapDoc& item : editor.docs) {
        if (!item.variationBaseId.isEmpty()) continue;
        append(item.id);
    }
    for (const MapDoc& item : editor.docs)
        if (!emitted.contains(item.id)) ordered.push_back(item);

    editor.docs = ordered;
    restoreActive(editor, activeId);
    if (MapDoc* moved = editor.mapById(mapId)) moved->dirty = true;
    editor.markDirty();
    return true;
}

QString duplicateMap(Editor& editor, const QString& mapId, QString* error)
{
    if (editor.mapIndexById(mapId) < 0) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return QString();
    }

    const QVector<MapDoc> canonical = canonicalMaps(editor);
    const QSet<QString> sourceIds = subtreeIds(editor, mapId);
    QVector<MapDoc> sources;
    for (const MapDoc& map : canonical)
        if (sourceIds.contains(map.id)) sources.push_back(map);
    if (sources.isEmpty()) return QString();

    QHash<QString, QString> mapIds;
    for (const MapDoc& source : sources) mapIds.insert(source.id, idGen());

    QHash<QString, QString> layerIds;
    QHash<QString, QString> objectIds;
    QSet<QString> reservedNames;
    QVector<MapDoc> copies;
    copies.reserve(sources.size());

    for (const MapDoc& source : sources) {
        MapDoc copy = source;
        copy.id = mapIds.value(source.id);
        copy.name = uniqueCopyName(editor, source.name, reservedNames);
        copy.rpgMakerMapId = 0; // duplicata recebe novo Map ID pelo sincronizador
        copy.rpgMakerImported = false;
        copy.parentId = mapIds.contains(source.parentId)
                            ? mapIds.value(source.parentId)
                            : source.parentId;
        copy.history.clear();
        copy.historyPtr = -1;
        copy.historyRevision = 0;
        copy.dirty = true;

        copy.layers.clear();
        for (const LayerPtr& layer : source.layers) {
            const LayerPtr cloned = cloneLayer(layer, true);
            copy.layers.push_back(cloned);
            collectLayerIdMap(layer, cloned, layerIds, objectIds);
        }
        if (layerIds.contains(source.activeLayerId))
            copy.activeLayerId = layerIds.value(source.activeLayerId);

        copies.push_back(copy);
    }

    for (MapDoc& copy : copies)
        for (const LayerPtr& layer : copy.layers)
            rewriteLayerProperties(layer, mapIds, layerIds, objectIds);

    int lastSourceIndex = -1;
    for (int i = 0; i < canonical.size(); ++i)
        if (sourceIds.contains(canonical[i].id)) lastSourceIndex = i;

    QVector<MapDoc> finalDocs;
    finalDocs.reserve(canonical.size() + copies.size());
    for (int i = 0; i < canonical.size(); ++i) {
        finalDocs.push_back(canonical[i]);
        if (i == lastSourceIndex)
            for (const MapDoc& copy : copies) finalDocs.push_back(copy);
    }

    editor.docs = finalDocs;
    const QString copiedRootId = mapIds.value(mapId);
    restoreActive(editor, copiedRootId);
    editor.session.selectedObjectId.clear();
    editor.session.selectedObjectIds.clear();
    editor.markDirty();
    return copiedRootId;
}

QString createVariation(Editor& editor, const QString& sourceMapId,
                        const QString& variationName, QString* error)
{
    const MapDoc* source = editor.mapById(sourceMapId);
    if (!source) {
        if (error) *error = QStringLiteral("Mapa de origem inexistente.");
        return QString();
    }

    const QString baseId = source->variationBaseId.isEmpty() ? source->id : source->variationBaseId;
    const MapDoc* base = editor.mapById(baseId);
    if (!base || !base->variationBaseId.isEmpty()) {
        if (error) *error = QStringLiteral("O mapa-base da variação não está disponível.");
        return QString();
    }

    QString label = variationName.trimmed();
    if (label.isEmpty()) label = QStringLiteral("Variação");
    const QString rootLabel = label;
    int suffix = 2;
    auto labelExists = [&](const QString& candidate) {
        for (const MapDoc& map : editor.docs)
            if (map.variationBaseId == baseId &&
                map.variationName.compare(candidate, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    while (labelExists(label)) label = QStringLiteral("%1 %2").arg(rootLabel).arg(suffix++);

    MapDoc copy = *source;
    copy.id = idGen();
    copy.variationBaseId = baseId;
    copy.variationName = label;
    copy.parentId = base->parentId;
    copy.name = editor.uniqueMapName(QStringLiteral("%1 — %2").arg(base->name, label));
    copy.rpgMakerMapId = 0;
    copy.rpgMakerImported = false;
    copy.history.clear();
    copy.historyPtr = -1;
    copy.historyRevision = 0;
    copy.dirty = true;

    QHash<QString, QString> mapIds;
    mapIds.insert(source->id, copy.id);
    QHash<QString, QString> layerIds;
    QHash<QString, QString> objectIds;
    copy.layers.clear();
    for (const LayerPtr& layer : source->layers) {
        const LayerPtr cloned = cloneLayer(layer, true);
        copy.layers.push_back(cloned);
        collectLayerIdMap(layer, cloned, layerIds, objectIds);
    }
    if (layerIds.contains(source->activeLayerId))
        copy.activeLayerId = layerIds.value(source->activeLayerId);
    for (const LayerPtr& layer : copy.layers)
        rewriteLayerProperties(layer, mapIds, layerIds, objectIds);

    int insertAt = editor.mapIndexById(baseId) + 1;
    for (int i = 0; i < editor.docs.size(); ++i)
        if (editor.docs[i].id == baseId || editor.docs[i].variationBaseId == baseId)
            insertAt = qMax(insertAt, i + 1);
    editor.docs.insert(qBound(0, insertAt, editor.docs.size()), copy);
    // Todos os estados do cenário carregam o índice de variações no manifesto
    // runtime. Ao adicionar uma variação, reexportamos o conjunto para que a
    // troca funcione a partir de qualquer estado.
    for (MapDoc& map : editor.docs)
        if (map.id == baseId || map.variationBaseId == baseId) map.dirty = true;

    restoreActive(editor, copy.id);
    editor.session.selectedObjectId.clear();
    editor.session.selectedObjectIds.clear();
    editor.markDirty();
    return copy.id;
}

bool deleteMap(Editor& editor, const QString& mapId, const MapDeletePolicy& policy,
               MapDeleteResult* result, QString* error)
{
    if (result) *result = MapDeleteResult();
    const MapDoc* target = editor.mapById(mapId);
    if (!target) {
        if (error) *error = QStringLiteral("Mapa inexistente.");
        return false;
    }

    QSet<QString> deleting;
    if (policy.children == MapDeleteChildrenPolicy::DeleteSubtree)
        deleting = subtreeIds(editor, mapId);
    else
        deleting.insert(mapId);

    // Variações pertencem semanticamente ao mapa-base. Ao remover o base elas
    // também são removidas, mesmo quando filhos normais são promovidos. Para
    // uma subárvore, isso vale para cada mapa-base contido nela.
    bool addedVariation = true;
    while (addedVariation) {
        addedVariation = false;
        for (const MapDoc& map : editor.docs) {
            if (map.variationBaseId.isEmpty() || !deleting.contains(map.variationBaseId) || deleting.contains(map.id))
                continue;
            deleting.insert(map.id);
            addedVariation = true;
        }
    }

    if (deleting.size() >= editor.docs.size()) {
        if (error) *error = QStringLiteral("O projeto precisa manter pelo menos um mapa.");
        return false;
    }

    const int refs = countExternalReferences(editor, deleting);
    if (result) {
        result->externalReferenceCount = refs;
        for (const MapDoc& map : editor.docs)
            if (deleting.contains(map.id)) result->deletedMapIds.push_back(map.id);
    }
    if (refs > 0 && policy.references == MapDeleteReferencePolicy::RejectReferenced) {
        if (error)
            *error = QStringLiteral("Há %1 referência(s) externa(s) aos mapas que seriam removidos.")
                         .arg(refs);
        return false;
    }

    const QString targetParent = target->parentId;
    const QString survivingScenarioBaseId = target->variationBaseId;
    const QString activeId = editor.doc() ? editor.doc()->id : QString();
    QVector<MapDoc> kept;
    kept.reserve(editor.docs.size() - deleting.size());
    for (const MapDoc& original : editor.docs) {
        if (deleting.contains(original.id)) continue;
        MapDoc map = original;
        if (policy.children == MapDeleteChildrenPolicy::PromoteChildren && map.parentId == mapId) {
            map.parentId = targetParent;
            map.dirty = true;
        }
        kept.push_back(map);
    }
    editor.docs = kept;
    if (!survivingScenarioBaseId.isEmpty()) {
        for (MapDoc& map : editor.docs)
            if (map.id == survivingScenarioBaseId || map.variationBaseId == survivingScenarioBaseId)
                map.dirty = true;
    }

    QString nextActive = activeId;
    if (deleting.contains(activeId) || editor.mapIndexById(activeId) < 0)
        nextActive = editor.docs.first().id;
    restoreActive(editor, nextActive);

    editor.session.selectedObjectId.clear();
    editor.session.selectedObjectIds.clear();
    normalizeHierarchy(editor);
    editor.markDirty();
    return true;
}

int normalizeHierarchy(Editor& editor)
{
    if (editor.docs.isEmpty()) return 0;
    int changed = 0;
    const QString activeId = editor.doc() ? editor.doc()->id : QString();

    for (MapDoc& map : editor.docs) {
        QString localError;
        if (!map.parentId.isEmpty() &&
            !canReparent(editor, map.id, map.parentId, &localError)) {
            map.parentId.clear();
            map.dirty = true;
            ++changed;
        }
    }

    // Repara relações de variação e faz cada variação acompanhar o mesmo pai
    // estrutural do seu mapa-base. O agrupamento visual não reutiliza parentId.
    for (MapDoc& map : editor.docs) {
        if (map.variationBaseId.isEmpty()) continue;
        const MapDoc* base = editor.mapById(map.variationBaseId);
        if (base && !base->variationBaseId.isEmpty())
            base = editor.mapById(base->variationBaseId);
        if (!base || base->id == map.id) {
            map.variationBaseId.clear();
            map.variationName.clear();
            map.dirty = true;
            ++changed;
            continue;
        }
        if (map.variationBaseId != base->id) {
            map.variationBaseId = base->id;
            map.dirty = true;
            ++changed;
        }
        if (map.parentId != base->parentId) {
            map.parentId = base->parentId;
            map.dirty = true;
            ++changed;
        }
        if (map.variationName.trimmed().isEmpty()) {
            map.variationName = QStringLiteral("Variação");
            map.dirty = true;
            ++changed;
        }
    }

    const QVector<MapDoc> ordered = canonicalMaps(editor);
    bool orderChanged = ordered.size() == editor.docs.size();
    if (orderChanged) {
        orderChanged = false;
        for (int i = 0; i < ordered.size(); ++i)
            if (ordered[i].id != editor.docs[i].id) {
                orderChanged = true;
                break;
            }
    }
    if (orderChanged) {
        editor.docs = ordered;
        ++changed;
    }

    restoreActive(editor, activeId);
    if (changed > 0) editor.markDirty();
    return changed;
}

} // namespace core::mapworkflow
