#include "AutotileTopology.h"

namespace core { namespace autotile {

int filterBlobMask(bool top, bool topRight, bool right, bool bottomRight,
                   bool bottom, bool bottomLeft, bool left, bool topLeft)
{
    bool tl = topLeft, tr = topRight, bl = bottomLeft, br = bottomRight;
    if (!left  || !top)    tl = false;
    if (!right || !top)    tr = false;
    if (!left  || !bottom) bl = false;
    if (!right || !bottom) br = false;

    int mask = 0;
    if (tl)     mask |= 1;
    if (top)    mask |= 2;
    if (tr)     mask |= 4;
    if (left)   mask |= 8;
    if (right)  mask |= 16;
    if (bl)     mask |= 32;
    if (bottom) mask |= 64;
    if (br)     mask |= 128;
    return mask;
}

bool terrainNeighbor(const LayerPtr& layer, int x, int y,
                     const QString& wangSetId, int colorId,
                     const TopologyPolicy& policy)
{
    if (!layer) return false;
    if (!layer->inBounds(x, y)) return policy.extendAtMapBoundary;
    const Cell& cell = layer->data2D[y][x];
    if (cell.isEmpty()) return false;

    // Empilhar Tiles: somente a peça VISÍVEL do topo participa das conexões.
    // Se um tile comum foi colocado por cima de um Autotile, o Autotile de
    // baixo fica congelado exatamente na variante que já tinha. Assim, inserir
    // ou remover um tile comum não faz a peça de baixo "se mexer".
    const TileRef& top = cell.constLast();
    return top.wangSetId == wangSetId && top.wangColorId == colorId;
}

int terrainNeighborMask(const LayerPtr& layer, int x, int y,
                        const QString& wangSetId, int colorId,
                        const TopologyPolicy& policy)
{
    const bool t  = terrainNeighbor(layer, x,     y - 1, wangSetId, colorId, policy);
    const bool b  = terrainNeighbor(layer, x,     y + 1, wangSetId, colorId, policy);
    const bool l  = terrainNeighbor(layer, x - 1, y,     wangSetId, colorId, policy);
    const bool r  = terrainNeighbor(layer, x + 1, y,     wangSetId, colorId, policy);
    const bool tl = terrainNeighbor(layer, x - 1, y - 1, wangSetId, colorId, policy);
    const bool tr = terrainNeighbor(layer, x + 1, y - 1, wangSetId, colorId, policy);
    const bool bl = terrainNeighbor(layer, x - 1, y + 1, wangSetId, colorId, policy);
    const bool br = terrainNeighbor(layer, x + 1, y + 1, wangSetId, colorId, policy);
    return filterBlobMask(t, tr, r, br, b, bl, l, tl);
}

bool isMapBoundaryCell(const LayerPtr& layer, int x, int y)
{
    return layer && layer->inBounds(x, y) &&
           (x == 0 || y == 0 || x == layer->cols - 1 || y == layer->rows - 1);
}

}} // namespace core::autotile
