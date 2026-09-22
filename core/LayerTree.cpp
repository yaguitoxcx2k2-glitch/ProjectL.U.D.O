#include "LayerTree.h"

#include <QHash>
#include <QSet>
#include <algorithm>

namespace core {
namespace {

bool validateNode(const LayerPtr& node, QSet<quintptr>& path, QSet<quintptr>& seenPointers,
                  QSet<QString>& seenIds, int& count, QString& error)
{
    if (!node) {
        error = QStringLiteral("A arvore contem uma camada nula.");
        return false;
    }
    const quintptr key = quintptr(node.data());
    if (path.contains(key)) {
        error = QStringLiteral("A hierarquia de camadas contem um ciclo.");
        return false;
    }
    if (seenPointers.contains(key)) {
        error = QStringLiteral("A mesma instancia de camada aparece mais de uma vez na arvore.");
        return false;
    }
    if (node->id.trimmed().isEmpty()) {
        error = QStringLiteral("Existe uma camada sem ID estavel.");
        return false;
    }
    if (seenIds.contains(node->id)) {
        error = QStringLiteral("Existem IDs de camada duplicados: %1").arg(node->id);
        return false;
    }
    if (!node->children.isEmpty() && !node->isContainer()) {
        error = QStringLiteral("A camada %1 possui filhos mas nao e um container.").arg(node->id);
        return false;
    }

    seenPointers.insert(key);
    seenIds.insert(node->id);
    path.insert(key);
    ++count;
    for (const LayerPtr& child : node->children) {
        if (!validateNode(child, path, seenPointers, seenIds, count, error)) return false;
    }
    path.remove(key);
    return true;
}

void collectNodes(const QVector<LayerPtr>& nodes, QHash<QString, LayerPtr>& byId,
                  QSet<quintptr>& visited)
{
    for (const LayerPtr& node : nodes) {
        if (!node) continue;
        const quintptr key = quintptr(node.data());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (!node->id.isEmpty()) byId.insert(node->id, node);
        collectNodes(node->children, byId, visited);
    }
}

void flattenRec(const QVector<LayerPtr>& nodes, QVector<LayerPtr>& out,
                QSet<quintptr>& visited)
{
    for (const LayerPtr& node : nodes) {
        if (!node) continue;
        const quintptr key = quintptr(node.data());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (node->isMask) {
            out.push_back(node);
            flattenRec(node->children, out, visited);
        } else if (node->type == LayerType::Group) {
            flattenRec(node->children, out, visited);
        } else {
            out.push_back(node);
        }
    }
}

bool subtreeContainsRec(const LayerPtr& root, const QString& id, QSet<quintptr>& visited)
{
    if (!root) return false;
    const quintptr key = quintptr(root.data());
    if (visited.contains(key)) return false;
    visited.insert(key);
    if (root->id == id) return true;
    for (const LayerPtr& child : root->children)
        if (subtreeContainsRec(child, id, visited)) return true;
    return false;
}

bool effectiveStateRec(const QVector<LayerPtr>& nodes, const QString& id,
                       bool parentVisible, bool parentLocked,
                       QSet<quintptr>& visited, LayerEffectiveState& out)
{
    for (const LayerPtr& node : nodes) {
        if (!node) continue;
        const quintptr key = quintptr(node.data());
        if (visited.contains(key)) continue;
        visited.insert(key);
        const bool visible = parentVisible && node->visible;
        const bool locked = parentLocked || node->locked;
        if (node->id == id) {
            out.found = true;
            out.visible = visible;
            out.locked = locked;
            return true;
        }
        if (effectiveStateRec(node->children, id, visible, locked, visited, out))
            return true;
    }
    return false;
}

} // namespace

LayerTreeValidationResult validateLayerTree(const QVector<LayerPtr>& roots)
{
    LayerTreeValidationResult result;
    QSet<quintptr> path;
    QSet<quintptr> seenPointers;
    QSet<QString> seenIds;
    for (const LayerPtr& root : roots) {
        if (!validateNode(root, path, seenPointers, seenIds, result.nodeCount, result.error)) {
            result.ok = false;
            return result;
        }
    }
    return result;
}

QVector<LayerPtr> flattenRenderableLayers(const QVector<LayerPtr>& roots)
{
    QVector<LayerPtr> out;
    QSet<quintptr> visited;
    flattenRec(roots, out, visited);
    return out;
}

bool layerSubtreeContains(const LayerPtr& root, const QString& id)
{
    QSet<quintptr> visited;
    return subtreeContainsRec(root, id, visited);
}

LayerEffectiveState layerEffectiveState(const QVector<LayerPtr>& roots, const QString& id)
{
    LayerEffectiveState state;
    if (id.isEmpty()) return state;
    QSet<quintptr> visited;
    effectiveStateRec(roots, id, true, false, visited, state);
    return state;
}

bool applyLayerTreePlacements(QVector<LayerPtr>& roots,
                              const QVector<LayerTreePlacement>& placements,
                              QString* error)
{
    const LayerTreeValidationResult current = validateLayerTree(roots);
    if (!current.ok) {
        if (error) *error = current.error;
        return false;
    }

    QHash<QString, LayerPtr> byId;
    QSet<quintptr> visited;
    collectNodes(roots, byId, visited);
    if (placements.size() != byId.size()) {
        if (error) *error = QStringLiteral("A reorganizacao nao contem exatamente todas as camadas do mapa.");
        return false;
    }

    QHash<QString, QString> parentById;
    QHash<QString, int> orderById;
    QSet<QString> placed;
    QHash<QString, QSet<int>> usedOrders;
    for (const LayerTreePlacement& placement : placements) {
        const QString id = placement.id.trimmed();
        const QString parentId = placement.parentId.trimmed();
        if (id.isEmpty() || !byId.contains(id)) {
            if (error) *error = QStringLiteral("A reorganizacao referencia uma camada inexistente.");
            return false;
        }
        if (placed.contains(id)) {
            if (error) *error = QStringLiteral("A camada %1 aparece duas vezes na reorganizacao.").arg(id);
            return false;
        }
        if (id == parentId) {
            if (error) *error = QStringLiteral("Uma camada nao pode ser pai de si mesma.");
            return false;
        }
        if (!parentId.isEmpty()) {
            const LayerPtr parent = byId.value(parentId);
            if (!parent || !parent->isContainer()) {
                if (error) *error = QStringLiteral("O pai %1 nao existe ou nao aceita filhos.").arg(parentId);
                return false;
            }
        }
        if (placement.order < 0 || usedOrders[parentId].contains(placement.order)) {
            if (error) *error = QStringLiteral("A ordem das camadas dentro de um grupo e invalida.");
            return false;
        }
        usedOrders[parentId].insert(placement.order);
        placed.insert(id);
        parentById.insert(id, parentId);
        orderById.insert(id, placement.order);
    }

    // Valida ciclos apenas pelo plano novo, antes de alterar qualquer children.
    for (auto it = parentById.constBegin(); it != parentById.constEnd(); ++it) {
        QSet<QString> chain;
        QString cursor = it.key();
        while (!cursor.isEmpty()) {
            if (chain.contains(cursor)) {
                if (error) *error = QStringLiteral("A reorganizacao criaria um ciclo de camadas.");
                return false;
            }
            chain.insert(cursor);
            cursor = parentById.value(cursor);
        }
    }

    QHash<QString, QVector<LayerPtr>> childrenByParent;
    for (const QString& id : placed)
        childrenByParent[parentById.value(id)].push_back(byId.value(id));
    for (auto it = childrenByParent.begin(); it != childrenByParent.end(); ++it) {
        QVector<LayerPtr>& siblings = it.value();
        std::sort(siblings.begin(), siblings.end(), [&](const LayerPtr& a, const LayerPtr& b) {
            return orderById.value(a->id) < orderById.value(b->id);
        });
    }

    // Commit atomico: somente agora desconectamos e religamos os filhos.
    // Guardamos a topologia anterior para rollback defensivo caso uma futura
    // mudança de validação torne possível falhar no pós-check.
    const QVector<LayerPtr> oldRoots = roots;
    QHash<QString, QVector<LayerPtr>> oldChildren;
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
        oldChildren.insert(it.key(), it.value()->children);

    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
        it.value()->children.clear();
    for (auto it = childrenByParent.constBegin(); it != childrenByParent.constEnd(); ++it) {
        if (it.key().isEmpty()) continue;
        byId.value(it.key())->children = it.value();
    }
    roots = childrenByParent.value(QString());

    const LayerTreeValidationResult after = validateLayerTree(roots);
    if (!after.ok) {
        for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
            it.value()->children = oldChildren.value(it.key());
        roots = oldRoots;
        if (error) *error = after.error;
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace core
