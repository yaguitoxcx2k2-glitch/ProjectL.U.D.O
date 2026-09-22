#include "Wang.h"
#include "AutoTileTables.h"
#include "TilesetCatalog.h"

#include <QRandomGenerator>
#include <algorithm>

namespace core { namespace wang {
namespace {

bool contextAllowsKey(const TerrainPaintContext& context, const QString& key,
                      int tilesetIdx)
{
    if (context.tilesetIdx >= 0 && tilesetIdx != context.tilesetIdx) return false;
    return context.allowedTileKeys.isEmpty() || context.allowedTileKeys.contains(key);
}

TerrainPaintContext contextForExistingCell(const LayerPtr& layer, int x, int y,
                                           const WangSet& set, int colorId,
                                           const TerrainPaintContext& fallback,
                                           int paintedColorId)
{
    if (!layer || !layer->inBounds(x, y)) return fallback;
    const Cell& cell = layer->data2D[y][x];
    if (cell.isEmpty()) return fallback;

    // Um tile comum no topo protege/congela qualquer Autotile que exista em
    //baixo. A topologia só enxerga a peça visualmente ativa da célula.
    const TileRef& top = cell.constLast();
    if (top.wangSetId != set.id || top.wangColorId != colorId) return fallback;

    if (fallback.sourceEditor) {
        if (const TilesetAutotile* autotile =
                tilesetAutotileAt(*fallback.sourceEditor, top.tilesetIdx, top.tx, top.ty)) {
            if (autotile->wangSetId == set.id && autotile->wangColorId == colorId)
                {
                    TerrainPaintContext resolved = contextForAutotile(*fallback.sourceEditor, top.tilesetIdx, autotile->id);
                    resolved.preserveCellStack = fallback.preserveCellStack;
                    return resolved;
                }
        }
    }

    // Compatibilidade para Wang legado ou celulas que ainda nao possuem uma
    // identidade de Autotile resolvivel. Nunca aplicamos o dominio visual do
    // pincel atual a outra cor/tileset por acidente.
    if (colorId != paintedColorId ||
        (fallback.tilesetIdx >= 0 && top.tilesetIdx != fallback.tilesetIdx)) {
        TerrainPaintContext legacy;
        legacy.sourceEditor = fallback.sourceEditor;
        legacy.tilesetIdx = top.tilesetIdx;
        legacy.preserveCellStack = fallback.preserveCellStack;
        return legacy;
    }
    return fallback;
}

/// Sorteio ponderado por Probability (equivalente a pickWeightedTile()).
bool pickWeighted(const QVector<TileRef>& candidates, TileRef* out,
                  const TerrainPaintContext& context)
{
    if (candidates.isEmpty()) return false;
    const Editor& ed = context.sourceEditor ? *context.sourceEditor : editor();
    double total = 0;
    QVector<double> weights;
    weights.reserve(candidates.size());
    for (const TileRef& candidate : candidates) {
        const double probability = ed.tileProb(candidate.tilesetIdx, candidate.tx, candidate.ty);
        weights.push_back(probability);
        total += probability;
    }
    if (total <= 0) {
        *out = candidates[QRandomGenerator::global()->bounded(candidates.size())];
        return true;
    }
    double roll = QRandomGenerator::global()->generateDouble() * total;
    for (int i = 0; i < candidates.size(); ++i) {
        roll -= weights[i];
        if (roll <= 0) { *out = candidates[i]; return true; }
    }
    *out = candidates.last();
    return true;
}

int wangTileIndex(const Cell& cell, const QString& setId, int colorId)
{
    if (cell.isEmpty()) return -1;
    const int topIndex = cell.size() - 1;
    const TileRef& top = cell.at(topIndex);
    return (top.wangSetId == setId && top.wangColorId == colorId) ? topIndex : -1;
}

int wangTileIndexForSet(const Cell& cell, const QString& setId)
{
    if (cell.isEmpty()) return -1;
    const int topIndex = cell.size() - 1;
    const TileRef& top = cell.at(topIndex);
    return (top.wangSetId == setId && top.wangColorId >= 0) ? topIndex : -1;
}

bool currentCellAlreadyMatches(const LayerPtr& layer, int x, int y,
                               const WangSet& set, int colorId, int mask,
                               const TerrainPaintContext& context)
{
    if (!layer || !layer->inBounds(x, y)) return false;
    const Cell& cell = layer->data2D[y][x];
    if (cell.isEmpty()) return false;
    const int tileIndex = wangTileIndex(cell, set.id, colorId);
    if (tileIndex < 0) return false;
    const TileRef& tile = cell.at(tileIndex);
    const QString key = WangSet::tileKeyOf(tile.tilesetIdx, tile.tx, tile.ty);
    if (!contextAllowsKey(context, key, tile.tilesetIdx)) return false;
    const auto it = set.tiles.constFind(key);
    return it != set.tiles.constEnd() && dataHasColor(it.value(), colorId) &&
           maskFromData(it.value(), colorId, set.type) == normalizeMaskForType(mask, set.type);
}

bool cellBelongsToContext(const LayerPtr& layer, int x, int y,
                          const WangSet& set, int colorId,
                          const TerrainPaintContext& context)
{
    if (!layer || !layer->inBounds(x, y)) return false;
    const Cell& cell = layer->data2D[y][x];
    if (cell.isEmpty()) return false;
    const int tileIndex = wangTileIndex(cell, set.id, colorId);
    if (tileIndex < 0) return false;
    const TileRef& tile = cell.at(tileIndex);
    const QString key = WangSet::tileKeyOf(tile.tilesetIdx, tile.tx, tile.ty);
    return contextAllowsKey(context, key, tile.tilesetIdx);
}

} // namespace

TerrainPaintContext contextForAutotile(const Editor& ed, int tilesetIdx,
                                       const QString& autotileId)
{
    TerrainPaintContext context;
    context.sourceEditor = &ed;
    context.tilesetIdx = tilesetIdx;
    context.autotileId = autotileId;

    const TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) return context;
    context.extendAtMapBoundary = autotile->extendAtMapBoundary;
    for (const QPoint& tile : tilesetAutotileTiles(ed, tilesetIdx, *autotile))
        context.allowedTileKeys.insert(WangSet::tileKeyOf(tilesetIdx, tile.x(), tile.y()));
    return context;
}

int filterMaskBlob(bool top, bool topRight, bool right, bool bottomRight,
                   bool bottom, bool bottomLeft, bool left, bool topLeft)
{
    return autotile::filterBlobMask(top, topRight, right, bottomRight,
                                    bottom, bottomLeft, left, topLeft);
}

int normalizeMaskForType(int mask, const QString& type)
{
    if (type == QLatin1String("edge"))
        return mask & (2 | 16 | 64 | 8); // T/R/B/L
    if (type == QLatin1String("corner"))
        return mask & (1 | 4 | 128 | 32); // TL/TR/BR/BL
    return autotile::filterBlobMask((mask & 2) != 0, (mask & 4) != 0,
                                    (mask & 16) != 0, (mask & 128) != 0,
                                    (mask & 64) != 0, (mask & 32) != 0,
                                    (mask & 8) != 0, (mask & 1) != 0);
}

int maskFromData(const WangTileData& d, int targetColorId, const QString& type)
{
    int mask = 0;
    if (d.tl == targetColorId) mask |= 1;
    if (d.t  == targetColorId) mask |= 2;
    if (d.tr == targetColorId) mask |= 4;
    if (d.l  == targetColorId) mask |= 8;
    if (d.r  == targetColorId) mask |= 16;
    if (d.bl == targetColorId) mask |= 32;
    if (d.b  == targetColorId) mask |= 64;
    if (d.br == targetColorId) mask |= 128;
    return normalizeMaskForType(mask, type);
}

bool isWangCell(const LayerPtr& layer, int x, int y, const QString& wangSetId, int colorId)
{
    if (!layer || !layer->inBounds(x, y)) return false;
    const Cell& c = layer->data2D[y][x];
    return wangTileIndex(c, wangSetId, colorId) >= 0;
}

int cellColor(const LayerPtr& layer, int x, int y, const QString& wangSetId)
{
    if (!layer || !layer->inBounds(x, y)) return -1;
    const Cell& c = layer->data2D[y][x];
    const int tileIndex = wangTileIndexForSet(c, wangSetId);
    return tileIndex >= 0 ? c.at(tileIndex).wangColorId : -1;
}

int neighborMask(const LayerPtr& layer, int x, int y, const QString& wangSetId, int colorId,
                 const TerrainPaintContext& context)
{
    autotile::TopologyPolicy policy;
    policy.extendAtMapBoundary = context.extendAtMapBoundary;
    return autotile::terrainNeighborMask(layer, x, y, wangSetId, colorId, policy);
}

bool dataHasColor(const WangTileData& d, int colorId)
{
    if (colorId < 0) return false;
    if (d.isIsolatedOf(colorId)) return true;
    return d.tl == colorId || d.t == colorId || d.tr == colorId || d.l == colorId ||
           d.r == colorId || d.bl == colorId || d.b == colorId || d.br == colorId;
}

QStringList allowedPositions(const QString& type)
{
    if (type == QLatin1String("corner"))
        return { QStringLiteral("tl"), QStringLiteral("tr"),
                 QStringLiteral("br"), QStringLiteral("bl") };
    if (type == QLatin1String("edge"))
        return { QStringLiteral("t"), QStringLiteral("r"),
                 QStringLiteral("b"), QStringLiteral("l") };
    return WangTileData::positions();
}

bool createTerrainFromPreset(WangSet& set, const WangPreset& preset, int tilesetIdx,
                             int originX, int originY, int* colorIdOut, int* countOut)
{
    int nextId = 1;
    for (const WangColor& c : set.colors) nextId = qMax(nextId, c.id + 1);
    WangColor col;
    col.id = nextId;
    col.name = QObject::tr("Terreno %1").arg(set.colors.size() + 1);
    col.color = QColor::fromHsv(QRandomGenerator::global()->bounded(360), 170, 210);
    set.colors.push_back(col);

    const QStringList allowed = allowedPositions(set.type);
    int count = 0;
    for (auto it = preset.positions.constBegin(); it != preset.positions.constEnd(); ++it) {
        const QStringList xy = it.key().split(QLatin1Char(','));
        if (xy.size() != 2) continue;
        const QString key = WangSet::tileKeyOf(tilesetIdx, originX + xy[0].toInt(),
                                                  originY + xy[1].toInt());
        WangTileData d = set.tiles.value(key);
        bool any = false;
        for (const QString& pos : allowed)
            if (it.value().contains(pos)) { d.set(pos, col.id); any = true; }
        if (it.value().contains(QStringLiteral("isolated"))) { d.isolatedColorId = col.id; any = true; }
        if (!any) continue;
        set.tiles.insert(key, d);
        ++count;
    }
    if (colorIdOut) *colorIdOut = col.id;
    if (countOut) *countOut = count;
    return true;
}

int removeColor(WangSet& set, int colorId)
{
    for (int i = 0; i < set.colors.size(); ++i)
        if (set.colors[i].id == colorId) { set.colors.remove(i); break; }

    int touched = 0;
    QStringList drop;
    for (auto it = set.tiles.begin(); it != set.tiles.end(); ++it) {
        WangTileData& d = it.value();
        bool changed = false;
        for (const QString& pos : WangTileData::positions())
            if (d.get(pos) == colorId) { d.set(pos, -1); changed = true; }
        if (d.isolatedColorId == colorId) { d.isolatedColorId = -1; changed = true; }
        if (changed) ++touched;
        if (d.isEmpty()) drop << it.key();
    }
    for (const QString& k : drop) set.tiles.remove(k);
    return touched;
}

bool findTileForMask(const WangSet& set, int colorId, int mask, TileRef* out,
                     const TerrainPaintContext& context)
{
    QVector<TileRef> matches;
    const int wantedMask = normalizeMaskForType(mask, set.type);
    for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
        if (!dataHasColor(it.value(), colorId)) continue;
        const int candidateMask = it.value().isIsolatedOf(colorId) ? 0
            : maskFromData(it.value(), colorId, set.type);
        if (candidateMask != wantedMask) continue;
        int ts, tx, ty;
        if (!WangSet::parseTileKey(it.key(), &ts, &tx, &ty)) continue;
        if (!contextAllowsKey(context, it.key(), ts)) continue;
        TileRef tile; tile.tilesetIdx = ts; tile.tx = tx; tile.ty = ty;
        matches.push_back(tile);
    }
    return pickWeighted(matches, out, context);
}

bool findTileBestMatch(const WangSet& set, int colorId, int mask, TileRef* out,
                       const TerrainPaintContext& context)
{
    if (findTileForMask(set, colorId, mask, out, context)) return true;
    int bestScore = -1;
    const int wantedMask = normalizeMaskForType(mask, set.type);
    QVector<TileRef> best;
    for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
        if (!dataHasColor(it.value(), colorId)) continue;
        int ts, tx, ty;
        if (!WangSet::parseTileKey(it.key(), &ts, &tx, &ty)) continue;
        if (!contextAllowsKey(context, it.key(), ts)) continue;
        const int candidateMask = it.value().isIsolatedOf(colorId) ? 0
            : maskFromData(it.value(), colorId, set.type);
        int score = 0;
        for (int bit = 0; bit < 8; ++bit)
            if (((candidateMask >> bit) & 1) == ((wantedMask >> bit) & 1)) ++score;
        TileRef tile; tile.tilesetIdx = ts; tile.tx = tx; tile.ty = ty;
        if (score > bestScore) { bestScore = score; best.clear(); best.push_back(tile); }
        else if (score == bestScore) best.push_back(tile);
    }
    return pickWeighted(best, out, context);
}

bool setCellToMask(const LayerPtr& layer, int x, int y, const WangSet& set, int colorId, int mask,
                   const TerrainPaintContext& context)
{
    if (currentCellAlreadyMatches(layer, x, y, set, colorId, mask, context)) return true;
    TileRef tile;
    if (!findTileBestMatch(set, colorId, mask, &tile, context)) return false;
    tile.wangSetId = set.id;
    tile.wangColorId = colorId;
    if (context.preserveCellStack) {
        Cell cell = layer->cellAt(x, y);
        // Empilhar Tiles trabalha sempre no topo da pilha. Se o topo já é o
        // mesmo Autotile, atualizamos a variante; se existe um tile comum no
        // topo, adicionamos o Autotile acima dele e não mexemos no que ficou
        // enterrado.
        const int existingIndex = wangTileIndex(cell, set.id, colorId);
        if (existingIndex >= 0) cell[existingIndex] = tile;
        else cell.push_back(tile);
        layer->setCell(x, y, cell);
    } else {
        Cell cell; cell.push_back(tile);
        layer->setCell(x, y, cell);
    }
    return true;
}

void updateNeighbors(const LayerPtr& layer, int cx, int cy, const WangSet& set,
                     int paintedColorId, const TerrainPaintContext& context)
{
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (!layer || !layer->inBounds(x, y)) continue;
            const int colorId = cellColor(layer, x, y, set.id);
            if (colorId < 0) continue;
            const TerrainPaintContext local = contextForExistingCell(
                layer, x, y, set, colorId, context, paintedColorId);
            const int mask = neighborMask(layer, x, y, set.id, colorId, local);
            setCellToMask(layer, x, y, set, colorId, mask, local);
        }
}

bool paintAt(const LayerPtr& layer, int gx, int gy, const WangSet& set, int colorId, bool erase,
             const TerrainPaintContext& context)
{
    if (!layer || !layer->inBounds(gx, gy)) return true;
    if (erase) {
        if (!layer->cellAt(gx, gy).isEmpty()) {
            if (context.preserveCellStack) {
                Cell cell = layer->cellAt(gx, gy);
                if (!cell.isEmpty()) cell.removeLast();
                layer->setCell(gx, gy, cell);
            } else {
                layer->setCell(gx, gy, Cell());
            }
            updateNeighbors(layer, gx, gy, set, colorId, context);
            if (context.sourceEditor)
                retileTerrainNeighborhood(*context.sourceEditor, layer, QRect(gx, gy, 1, 1));
        }
        return true;
    }
    const int mask = neighborMask(layer, gx, gy, set.id, colorId, context);
    if (!setCellToMask(layer, gx, gy, set, colorId, mask, context)) return false;
    updateNeighbors(layer, gx, gy, set, colorId, context);
    if (context.sourceEditor)
        retileTerrainNeighborhood(*context.sourceEditor, layer, QRect(gx, gy, 1, 1));
    return true;
}

int paintCells(const Editor& ed, const LayerPtr& layer, const QVector<QPoint>& cells,
               const WangSet& set, int colorId, bool erase,
               const TerrainPaintContext& context)
{
    if (!layer || layer->type != LayerType::Tile || cells.isEmpty()) return 0;
    QRect dirty;
    int changed = 0;
    QSet<qint64> seen;
    for (const QPoint& p : cells) {
        if (!layer->inBounds(p.x(), p.y())) continue;
        const qint64 key = (qint64(p.y()) << 32) ^ quint32(p.x());
        if (seen.contains(key)) continue;
        seen.insert(key);
        const Cell before = layer->cellAt(p.x(), p.y());
        if (erase) {
            if (!before.isEmpty()) {
                if (context.preserveCellStack) {
                    Cell after = before;
                    after.removeLast();
                    layer->setCell(p.x(), p.y(), after);
                } else {
                    layer->setCell(p.x(), p.y(), Cell());
                }
            }
        } else {
            const int mask = neighborMask(layer, p.x(), p.y(), set.id, colorId, context);
            if (!setCellToMask(layer, p.x(), p.y(), set, colorId, mask, context)) continue;
        }
        if (layer->cellAt(p.x(), p.y()) != before) ++changed;
        dirty = dirty.isNull() ? QRect(p, QSize(1,1)) : dirty.united(QRect(p, QSize(1,1)));
    }
    if (!dirty.isNull()) retileTerrainNeighborhood(ed, layer, dirty);
    return changed;
}

namespace {
int retileCells(const LayerPtr& layer, const WangSet& set, int colorId,
                const TerrainPaintContext& context, bool boundaryOnly)
{
    if (!layer || layer->cols <= 0 || layer->rows <= 0) return 0;
    int changed = 0;
    for (int y = 0; y < layer->rows; ++y) {
        for (int x = 0; x < layer->cols; ++x) {
            if (boundaryOnly && !autotile::isMapBoundaryCell(layer, x, y)) continue;
            if (!cellBelongsToContext(layer, x, y, set, colorId, context)) continue;
            const Cell before = layer->cellAt(x, y);
            const int mask = neighborMask(layer, x, y, set.id, colorId, context);
            if (!setCellToMask(layer, x, y, set, colorId, mask, context)) continue;
            if (layer->cellAt(x, y) != before) ++changed;
        }
    }
    return changed;
}
} // namespace

int retileBoundaryCells(const LayerPtr& layer, const WangSet& set, int colorId,
                        const TerrainPaintContext& context)
{
    return retileCells(layer, set, colorId, context, true);
}

int retileTerrainCells(const LayerPtr& layer, const WangSet& set, int colorId,
                       const TerrainPaintContext& context)
{
    return retileCells(layer, set, colorId, context, false);
}

namespace {
bool retileExistingTerrainCell(const Editor& ed, const LayerPtr& layer, int x, int y)
{
    if (!layer || !layer->inBounds(x, y)) return false;
    const Cell before = layer->cellAt(x, y);
    if (before.isEmpty()) return false;

    // Não procure Autotiles enterrados na pilha. Quando há um tile comum no
    // topo, o Autotile abaixo deve permanecer exatamente como estava até voltar
    // a ser a peça ativa/visível da célula.
    const int terrainIndex = before.size() - 1;
    const TileRef terrainTile = before.at(terrainIndex);
    if (terrainTile.wangSetId.isEmpty() || terrainTile.wangColorId < 0) return false;
    const WangSet* set = wangSetById(ed, terrainTile.wangSetId);
    if (!set || !set->colorById(terrainTile.wangColorId)) return false;

    TerrainPaintContext context;
    context.sourceEditor = &ed;
    context.tilesetIdx = terrainTile.tilesetIdx;
    context.preserveCellStack = before.size() > 1;
    if (const TilesetAutotile* autotile = tilesetAutotileAt(ed, terrainTile.tilesetIdx, terrainTile.tx, terrainTile.ty)) {
        if (autotile->wangSetId == terrainTile.wangSetId && autotile->wangColorId == terrainTile.wangColorId)
            {
                const bool preserveStack = context.preserveCellStack;
                context = contextForAutotile(ed, terrainTile.tilesetIdx, autotile->id);
                context.preserveCellStack = preserveStack;
            }
    }

    const int mask = neighborMask(layer, x, y, set->id, terrainTile.wangColorId, context);
    if (!setCellToMask(layer, x, y, *set, terrainTile.wangColorId, mask, context)) return false;
    return layer->cellAt(x, y) != before;
}
}

int retileTerrainNeighborhood(const Editor& ed, const LayerPtr& layer,
                              const QRect& changedCells)
{
    if (!layer || layer->type != LayerType::Tile || changedCells.isEmpty()) return 0;
    const QRect mapRect(0, 0, layer->cols, layer->rows);
    const QRect affected = changedCells.normalized().adjusted(-1, -1, 1, 1).intersected(mapRect);
    int changed = 0;
    for (int y = affected.top(); y <= affected.bottom(); ++y)
        for (int x = affected.left(); x <= affected.right(); ++x)
            if (retileExistingTerrainCell(ed, layer, x, y)) ++changed;
    return changed;
}

int retileTerrainLayer(const Editor& ed, const LayerPtr& layer)
{
    if (!layer || layer->type != LayerType::Tile) return 0;
    int changed = 0;
    for (int y = 0; y < layer->rows; ++y)
        for (int x = 0; x < layer->cols; ++x)
            if (retileExistingTerrainCell(ed, layer, x, y)) ++changed;
    return changed;
}

// ------------------------------------------------------------------ presets
WangPreset builtinTerrenos12x4()
{
    // Geometria importada do preset fornecido pelo projeto:
    // source name = "12x4", source id = "i4e4dm6h1qhmtf5sw48".
    // O nome interno permanece "Terrenos (12x4)" porque esse é o contrato
    // exposto pelo Gerenciador/importador. As duas posições ausentes no JSON
    // (10,1 e 0,3) permanecem intencionalmente sem rótulos.
    WangPreset p;
    p.id = QStringLiteral("builtin-terrenos-12x4");
    p.name = QStringLiteral("Terrenos (12x4)");
    p.type = QStringLiteral("mixed");
    p.w = 12;
    p.h = 4;
    p.builtin = true;
    p.positions.insert(QStringLiteral("0,0"), QSet<QString>{ QStringLiteral("b") });
    p.positions.insert(QStringLiteral("1,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("2,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,0"), QSet<QString>{ QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("4,0"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("5,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("6,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("7,0"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("8,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("9,0"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("10,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("11,0"), QSet<QString>{ QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("0,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("1,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("2,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("4,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("5,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("6,1"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("7,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("8,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("9,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("11,1"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("0,2"), QSet<QString>{ QStringLiteral("t") });
    p.positions.insert(QStringLiteral("1,2"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r") });
    p.positions.insert(QStringLiteral("2,2"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,2"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("4,2"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("5,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("6,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("7,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("8,2"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("9,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("10,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("11,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("1,3"), QSet<QString>{ QStringLiteral("r") });
    p.positions.insert(QStringLiteral("2,3"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,3"), QSet<QString>{ QStringLiteral("l") });
    p.positions.insert(QStringLiteral("4,3"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("5,3"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("6,3"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("7,3"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("8,3"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r") });
    p.positions.insert(QStringLiteral("9,3"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("10,3"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("b"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("11,3"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("l") });
    return p;
}

WangPreset builtinAutotile4x4()
{
    // Geometria importada literalmente do preset fornecido pelo projeto:
    // source name = "4x4", source id = "8n3kxvg749omtf4unvc".
    // O slot 0,3 permanece sem rótulos no preset bruto; a convenção de
    // importação o promove a tile isolado sem alterar as 15 variantes.
    WangPreset p;
    p.id = QStringLiteral("builtin-autotile-4x4");
    p.name = QStringLiteral("4x4");
    p.type = QStringLiteral("mixed");
    p.w = 4;
    p.h = 4;
    p.builtin = true;
    p.positions.insert(QStringLiteral("0,0"), QSet<QString>{ QStringLiteral("b") });
    p.positions.insert(QStringLiteral("1,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("2,0"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,0"), QSet<QString>{ QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("0,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("1,1"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b") });
    p.positions.insert(QStringLiteral("2,1"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,1"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("b"), QStringLiteral("bl"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("0,2"), QSet<QString>{ QStringLiteral("t") });
    p.positions.insert(QStringLiteral("1,2"), QSet<QString>{ QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r") });
    p.positions.insert(QStringLiteral("2,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("tr"), QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,2"), QSet<QString>{ QStringLiteral("tl"), QStringLiteral("t"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("1,3"), QSet<QString>{ QStringLiteral("r") });
    p.positions.insert(QStringLiteral("2,3"), QSet<QString>{ QStringLiteral("r"), QStringLiteral("l") });
    p.positions.insert(QStringLiteral("3,3"), QSet<QString>{ QStringLiteral("l") });
    return p;
}

WangPreset builtinBlob47ForGrid(int columns, int rows)
{
    WangPreset p;
    if (columns <= 0 || rows <= 0 || columns * rows != 48) return p;
    p.id = (columns == 8 && rows == 6)
        ? QStringLiteral("builtin-blob47")
        : QStringLiteral("builtin-blob47-%1x%2").arg(columns).arg(rows);
    p.name = QStringLiteral("Blob 47 clássico (%1x%2)").arg(columns).arg(rows);
    p.type = QStringLiteral("mixed");
    p.w = columns; p.h = rows;
    p.builtin = true;
    static const struct { const char* pos; int bit; } bits[] = {
        { "tl", 1 }, { "t", 2 }, { "tr", 4 }, { "l", 8 },
        { "r", 16 }, { "bl", 32 }, { "b", 64 }, { "br", 128 }
    };
    const QHash<int,int>& table = blob47::pickTile();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        const int mask = it.key(), idx = it.value();
        if (idx < 0 || idx >= columns * rows) continue;
        const int tx = idx % columns, ty = idx / columns;
        QSet<QString> positions;
        for (const auto& bit : bits)
            if (mask & bit.bit) positions.insert(QString::fromLatin1(bit.pos));
        // O preset Blob47 bruto preserva o contrato histórico: a máscara 0
        // fica sem rótulo. A importação automática promove o primeiro tile
        // da última fileira para a variante isolada em TilesetCatalog.
        if (!positions.isEmpty())
            p.positions.insert(QStringLiteral("%1,%2").arg(tx).arg(ty), positions);
    }
    return p;
}

WangPreset builtinBlob47()
{
    return builtinBlob47ForGrid(8, 6);
}

bool presetIsCompleteForGrid(const WangPreset& preset, int columns, int rows)
{
    if (columns <= 0 || rows <= 0 || preset.w != columns || preset.h != rows) return false;
    QSet<int> masks;
    for (auto it = preset.positions.constBegin(); it != preset.positions.constEnd(); ++it) {
        const QStringList xy = it.key().split(QLatin1Char(','));
        if (xy.size() != 2) continue;
        bool okX = false, okY = false;
        const int x = xy.at(0).toInt(&okX), y = xy.at(1).toInt(&okY);
        if (!okX || !okY || x < 0 || y < 0 || x >= columns || y >= rows) continue;
        int mask = 0;
        const QSet<QString>& flags = it.value();
        if (flags.contains(QStringLiteral("tl"))) mask |= 1;
        if (flags.contains(QStringLiteral("t")))  mask |= 2;
        if (flags.contains(QStringLiteral("tr"))) mask |= 4;
        if (flags.contains(QStringLiteral("l")))  mask |= 8;
        if (flags.contains(QStringLiteral("r")))  mask |= 16;
        if (flags.contains(QStringLiteral("bl"))) mask |= 32;
        if (flags.contains(QStringLiteral("b")))  mask |= 64;
        if (flags.contains(QStringLiteral("br"))) mask |= 128;
        if (flags.contains(QStringLiteral("isolated"))) mask = 0;
        else if (flags.isEmpty()) continue;
        masks.insert(normalizeMaskForType(mask, preset.type));
    }
    const QVector<int> expected = expectedMasks(preset.type);
    for (int mask : expected)
        if (!masks.contains(mask)) return false;
    return !expected.isEmpty();
}

int applyPreset(WangSet& set, const WangPreset& preset, int tilesetIdx,
                int originX, int originY, int colorId, const QRect& allowedRegion,
                const QSet<QString>& allowedTileKeys)
{
    if (colorId < 0 || preset.w <= 0 || preset.h <= 0) return 0;
    int touched = 0;
    const QStringList allowed = allowedPositions(set.type);

    // Aplicacao exata e nao destrutiva: dentro do footprint do preset, apaga
    // somente os rotulos DA COR ATIVA e reconstroi essa cor a partir do
    // preset. Rotulos de outras cores/Terrains permanecem byte-a-byte.
    for (int dy = 0; dy < preset.h; ++dy) {
        for (int dx = 0; dx < preset.w; ++dx) {
            const int tx = originX + dx;
            const int ty = originY + dy;
            if (allowedRegion.isValid() && !allowedRegion.contains(tx, ty)) continue;
            const QString key = WangSet::tileKeyOf(tilesetIdx, tx, ty);
            if (!allowedTileKeys.isEmpty() && !allowedTileKeys.contains(key)) continue;
            WangTileData d = set.tiles.value(key);
            const WangTileData before = d;

            // Limpa a cor ativa de TODAS as posicoes, inclusive as que nao
            // pertencem mais ao tipo atual (ex.: mixed -> edge).
            for (const QString& pos : WangTileData::positions())
                if (d.get(pos) == colorId) d.set(pos, -1);
            if (d.isolatedColorId == colorId) d.isolatedColorId = -1;

            const QSet<QString> flags = preset.positions.value(
                QStringLiteral("%1,%2").arg(dx).arg(dy));
            for (const QString& pos : allowed)
                if (flags.contains(pos)) d.set(pos, colorId);
            if (flags.contains(QStringLiteral("isolated")))
                d.isolatedColorId = colorId;

            const bool changed = d.tl != before.tl || d.t != before.t || d.tr != before.tr ||
                                 d.l != before.l || d.r != before.r ||
                                 d.bl != before.bl || d.b != before.b || d.br != before.br ||
                                 d.isolatedColorId != before.isolatedColorId;
            if (!changed) continue;
            if (d.isEmpty()) set.tiles.remove(key);
            else set.tiles.insert(key, d);
            ++touched;
        }
    }
    return touched;
}

QVector<int> expectedMasks(const QString& type)
{
    if (type == QLatin1String("edge")) {
        QVector<int> out;
        const int bits[] = {2, 16, 64, 8};
        for (int combo = 0; combo < 16; ++combo) {
            int mask = 0;
            for (int i = 0; i < 4; ++i) if (combo & (1 << i)) mask |= bits[i];
            out.push_back(mask);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    if (type == QLatin1String("corner")) {
        QVector<int> out;
        const int bits[] = {1, 4, 128, 32};
        for (int combo = 0; combo < 16; ++combo) {
            int mask = 0;
            for (int i = 0; i < 4; ++i) if (combo & (1 << i)) mask |= bits[i];
            out.push_back(mask);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    QVector<int> out = blob47::pickTile().keys().toVector();
    if (!out.contains(0)) out.push_back(0);
    std::sort(out.begin(), out.end());
    return out;
}

QVector<int> missingMasks(const WangSet& set, int colorId,
                         const QSet<QString>& allowedTileKeys)
{
    QSet<int> present;
    for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
        if (!allowedTileKeys.isEmpty() && !allowedTileKeys.contains(it.key())) continue;
        if (!dataHasColor(it.value(), colorId)) continue;
        if (it.value().isIsolatedOf(colorId)) present.insert(0);
        present.insert(maskFromData(it.value(), colorId, set.type));
    }
    QVector<int> missing;
    for (int mask : expectedMasks(set.type)) if (!present.contains(mask)) missing.push_back(mask);
    return missing;
}

QHash<int, QStringList> duplicateMasks(const WangSet& set, int colorId,
                                       const QSet<QString>& allowedTileKeys)
{
    QHash<int, QStringList> byMask;
    for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
        if (!allowedTileKeys.isEmpty() && !allowedTileKeys.contains(it.key())) continue;
        if (!dataHasColor(it.value(), colorId)) continue;
        const int mask = it.value().isIsolatedOf(colorId) ? 0 : maskFromData(it.value(), colorId, set.type);
        byMask[mask] << it.key();
    }
    QHash<int, QStringList> duplicates;
    for (auto it = byMask.constBegin(); it != byMask.constEnd(); ++it)
        if (it.value().size() > 1) duplicates.insert(it.key(), it.value());
    return duplicates;
}

double coverage(const WangSet& set, int colorId,
                const QSet<QString>& allowedTileKeys)
{
    const QVector<int> expected = expectedMasks(set.type);
    if (expected.isEmpty()) return 0;
    QSet<int> present;
    for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
        if (!allowedTileKeys.isEmpty() && !allowedTileKeys.contains(it.key())) continue;
        if (!dataHasColor(it.value(), colorId)) continue;
        if (it.value().isIsolatedOf(colorId)) present.insert(0);
        present.insert(maskFromData(it.value(), colorId, set.type));
    }
    int hit = 0;
    for (int mask : expected) if (present.contains(mask)) ++hit;
    return double(hit) / expected.size();
}

}} // namespace core::wang
