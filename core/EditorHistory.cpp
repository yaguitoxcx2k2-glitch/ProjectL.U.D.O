#include "Editor.h"
#include "LayerTree.h"

#include <utility>
#include <QPainter>

namespace core {

static const int kMaxHistory = 150;

LayerSnapshot Editor::snapshotLayer(const LayerPtr& l) const
{
    LayerSnapshot s;
    if (!l) return s;
    s.offsetx = l->offsetx;
    s.offsety = l->offsety;
    s.imageMask = l->imageMask;
    s.imageMaskEnabled = l->imageMaskEnabled;
    if (l->type == LayerType::Tile) s.data2D = l->data2D;
    else if (l->type == LayerType::Object) s.objects = l->objects;
    else if (l->type == LayerType::Image) s.image = l->image;
    return s;
}

void Editor::applySnapshot(const LayerPtr& l, const LayerSnapshot& s)
{
    if (!l) return;
    l->offsetx = s.offsetx;
    l->offsety = s.offsety;
    l->imageMask = s.imageMask;
    l->imageMaskEnabled = s.imageMaskEnabled;
    if (l->type == LayerType::Tile) {
        l->data2D = s.data2D;
        l->rows = l->data2D.size();
        l->cols = l->data2D.isEmpty() ? 0 : l->data2D[0].size();
    } else if (l->type == LayerType::Object) {
        l->objects = s.objects;
    } else if (l->type == LayerType::Image) {
        l->image = s.image;
        l->imagewidth = l->image.width();
        l->imageheight = l->image.height();
    }
}

static QVector<LayerPtr> deepCopyKeepIds(const QVector<LayerPtr>& src)
{
    QVector<LayerPtr> out;
    out.reserve(src.size());
    for (const LayerPtr& l : src) out.push_back(cloneLayer(l, false));
    return out;
}

DocSnapshot Editor::snapshotDoc() const
{
    DocSnapshot s;
    const MapDoc* d = doc();
    if (!d) return s;
    s.name = d->name;
    s.parentId = d->parentId;
    s.variationBaseId = d->variationBaseId;
    s.variationName = d->variationName;
    s.rpgMakerMapId = d->rpgMakerMapId;
    s.rpgMakerImported = d->rpgMakerImported;
    s.map = d->map;
    s.layers = deepCopyKeepIds(d->layers);
    s.activeLayerIdx = d->activeLayerIdx;
    s.activeLayerId = d->activeLayerId;
    s.rpgMakerRegions = d->rpgMakerRegions;
    s.rpgMakerRegionsAuthored = d->rpgMakerRegionsAuthored;
    s.reflectionSettings = d->reflectionSettings;
    return s;
}

void Editor::applyDocSnapshot(const DocSnapshot& s)
{
    MapDoc* d = doc();
    if (!d) return;
    d->name = s.name;
    d->parentId = s.parentId;
    d->variationBaseId = s.variationBaseId;
    d->variationName = s.variationName;
    d->rpgMakerMapId = s.rpgMakerMapId;
    d->rpgMakerImported = s.rpgMakerImported;
    d->map = s.map;
    d->layers = deepCopyKeepIds(s.layers);
    d->activeLayerIdx = s.activeLayerIdx;
    d->activeLayerId = s.activeLayerId;
    d->rpgMakerRegions = s.rpgMakerRegions;
    d->rpgMakerRegionsAuthored = s.rpgMakerRegionsAuthored;
    d->reflectionSettings = s.reflectionSettings;
    d->pruneRegions();
    if (d->activeLayerId.isEmpty() && activeLayer()) d->activeLayerId = activeLayer()->id;
    session.selectedLayerId = d->activeLayerId;
    emit layersChanged();
    emit mapChanged();
    emit selectionChanged();
}

Editor::EditSession Editor::beginLayerEdit(const LayerPtr& layer)
{
    EditSession s;
    s.layer = layer ? layer : activeLayer();
    if (!s.layer) return s;
    s.before = snapshotLayer(s.layer);
    s.valid = true;
    return s;
}

void Editor::markLayerEditRasterDirty(EditSession& s, const QRect& localRect, bool mask)
{
    if (!s.valid || !s.layer || localRect.isEmpty()) return;
    if (s.rasterDirtySet && s.rasterDirtyMask != mask) {
        // Um stroke normal nunca alterna conteúdo/máscara. Se um chamador fizer
        // isso, desabilitamos o diff parcial e caímos no snapshot completo.
        s.rasterDirtySet = false;
        s.rasterDirty = QRect();
        return;
    }
    s.rasterDirtyMask = mask;
    s.rasterDirty = s.rasterDirtySet ? s.rasterDirty.united(localRect) : localRect;
    s.rasterDirtySet = true;
}

static bool sameSnapshot(const LayerSnapshot& a, const LayerSnapshot& b)
{
    if (a.data2D.size() != b.data2D.size()) return false;
    for (int y = 0; y < a.data2D.size(); ++y) {
        if (a.data2D[y].size() != b.data2D[y].size()) return false;
        for (int x = 0; x < a.data2D[y].size(); ++x)
            if (a.data2D[y][x] != b.data2D[y][x]) return false;
    }
    if (a.objects.size() != b.objects.size()) return false;
    for (int i = 0; i < a.objects.size(); ++i) {
        const MapObject& o1 = a.objects[i];
        const MapObject& o2 = b.objects[i];
        if (o1.id != o2.id || o1.x != o2.x || o1.y != o2.y || o1.w != o2.w ||
            o1.h != o2.h || o1.name != o2.name || o1.tiles != o2.tiles ||
            o1.visible != o2.visible || o1.rotation != o2.rotation ||
            o1.rotationFilter != o2.rotationFilter || o1.scaleFilter != o2.scaleFilter) return false;
    }
    // QImage compartilha pixels por COW. Snapshots do mesmo conteúdo mantêm o
    // cacheKey; qualquer pintura que detach/modifique a imagem gera outro.
    if (a.image.cacheKey() != b.image.cacheKey()) return false;
    if (a.imageMask.cacheKey() != b.imageMask.cacheKey()) return false;
    if (a.imageMaskEnabled != b.imageMaskEnabled) return false;
    if (a.offsetx != b.offsetx || a.offsety != b.offsety) return false;
    return true;
}

void Editor::pruneHistory(MapDoc* d)
{
    while (d->history.size() > kMaxHistory) { d->history.removeFirst(); --d->historyPtr; }
    if (d->historyPtr < -1) d->historyPtr = -1;
}

void Editor::commitLayerEdit(EditSession& s, const QString& label)
{
    MapDoc* d = doc();
    if (!s.valid || !s.layer || !d) return;

    HistoryEntry e;
    e.document = false;
    e.layerId = s.layer->id;
    e.label = label.isEmpty() ? QStringLiteral("Pintar em “%1”").arg(s.layer->name) : label;

    const LayerSnapshot after = snapshotLayer(s.layer);

    // Conteúdo de Tile Layer é o caminho mais frequente do editor. A revisão
    // anterior fazia duas varreduras completas da grade ao terminar CADA
    // pincelada: sameSnapshot() e depois a construção do diff. Em mapas
    // grandes isso gerava a sensação de microtravadas. Como QVector/QList usa
    // compartilhamento implícito, linhas que não foram tocadas continuam
    // iguais e podem ser descartadas com uma comparação de linha antes de
    // visitar cada célula.
    bool changed = false;
    bool tileMetadataSame = true;
    bool rasterHandled = false;

    if (s.rasterDirtySet) {
        const QImage& beforeRaster = s.rasterDirtyMask ? s.before.imageMask : s.before.image;
        const QImage& afterRaster = s.rasterDirtyMask ? after.imageMask : after.image;
        const bool targetCompatible = !beforeRaster.isNull() && !afterRaster.isNull() &&
                                      beforeRaster.size() == afterRaster.size();
        QRect dirty = s.rasterDirty;
        if (targetCompatible) dirty = dirty.intersected(beforeRaster.rect());

        if (targetCompatible && !dirty.isEmpty()) {
            const QImage beforePatch = beforeRaster.copy(dirty);
            const QImage afterPatch = afterRaster.copy(dirty);
            if (beforePatch != afterPatch) {
                e.rasterDiff = true;
                e.rasterMaskDiff = s.rasterDirtyMask;
                e.rasterRect = dirty;
                e.beforeRaster = beforePatch;
                e.afterRaster = afterPatch;
                changed = true;
                rasterHandled = true;
            } else {
                // MapView marca dirty somente para o alvo raster do stroke. Se
                // o patch não mudou, não há nada para colocar no histórico.
                s.valid = false;
                return;
            }
        }
    }

    if (rasterHandled) {
        // O histórico já recebeu apenas o patch local; não carregue snapshots
        // inteiros da imagem para cada pincelada.
    } else if (s.layer->type == LayerType::Tile) {
        tileMetadataSame = s.before.offsetx == after.offsetx &&
                           s.before.offsety == after.offsety &&
                           s.before.imageMask.cacheKey() == after.imageMask.cacheKey() &&
                           s.before.imageMaskEnabled == after.imageMaskEnabled;

        bool compatible = s.before.data2D.size() == after.data2D.size();
        int totalCells = 0;
        QVector<TileHistoryChange> changes;
        if (compatible) {
            changes.reserve(128);
            for (int y = 0; y < s.before.data2D.size(); ++y) {
                const auto& beforeRow = s.before.data2D[y];
                const auto& afterRow = after.data2D[y];
                if (beforeRow.size() != afterRow.size()) { compatible = false; break; }
                totalCells += beforeRow.size();
                if (beforeRow == afterRow) continue;
                for (int x = 0; x < beforeRow.size(); ++x) {
                    if (beforeRow[x] == afterRow[x]) continue;
                    changes.push_back(TileHistoryChange{x, y, beforeRow[x], afterRow[x]});
                }
            }
        }

        changed = !changes.isEmpty() || !tileMetadataSame || !compatible;
        if (!changed) { s.valid = false; return; }

        const int diffLimit = qMax(256, totalCells / 3);
        if (compatible && tileMetadataSame && !changes.isEmpty() && changes.size() <= diffLimit) {
            e.tileDiff = true;
            e.tileChanges = std::move(changes);
        } else {
            e.beforeLayer = s.before;
            e.afterLayer = after;
        }
    } else {
        if (sameSnapshot(s.before, after)) { s.valid = false; return; }
        changed = true;
        e.beforeLayer = s.before;
        e.afterLayer = after;
    }

    if (!changed) { s.valid = false; return; }

    d->history.resize(d->historyPtr + 1);
    d->history.push_back(std::move(e));
    d->historyPtr = d->history.size() - 1;
    pruneHistory(d);
    s.valid = false;
    ++d->historyRevision;
    markDirty();
    emit historyChanged();

    // Pintar/mover conteúdo não é uma mudança ESTRUTURAL da árvore de
    // camadas. Emitir layersChanged aqui reconstruía o painel de camadas,
    // cancelava interações do MapView e disparava um repaint completo depois
    // de cada stroke. Só atualizamos a árvore quando o estado visual que ela
    // realmente exibe (presença/ativação de máscara) mudou.
    const bool layerUiChanged = s.before.imageMask.isNull() != after.imageMask.isNull() ||
                                s.before.imageMaskEnabled != after.imageMaskEnabled;
    if (layerUiChanged) emit layersChanged();
    emit mapChanged();
}

void Editor::pushDocHistory(const DocSnapshot& before, const QString& label)
{
    MapDoc* d = doc();
    if (!d) return;
    HistoryEntry e;
    e.document = true;
    e.label = label;
    e.beforeDoc = before;
    e.afterDoc = snapshotDoc();
    d->history.resize(d->historyPtr + 1);
    d->history.push_back(e);
    d->historyPtr = d->history.size() - 1;
    pruneHistory(d);
    ++d->historyRevision;
    markDirty();
    emit historyChanged();
}

void Editor::pushDocTilesetHistory(const DocSnapshot& beforeDoc, const QVector<Tileset>& beforeTilesets,
                                   int beforeActiveTilesetIdx, const TilesetSelection& beforeTsSel,
                                   const CustomStamp& beforeCustomStamp, const QString& label)
{
    MapDoc* d = doc();
    if (!d) return;
    HistoryEntry e;
    e.document = true;
    e.tilesetSnapshot = true;
    e.label = label;
    e.beforeDoc = beforeDoc;
    e.afterDoc = snapshotDoc();
    e.beforeTilesets = beforeTilesets;
    e.afterTilesets = tilesets;
    e.beforeActiveTilesetIdx = beforeActiveTilesetIdx;
    e.afterActiveTilesetIdx = session.activeTilesetIdx;
    e.beforeTsSel = beforeTsSel;
    e.afterTsSel = session.tsSel;
    e.beforeCustomStamp = beforeCustomStamp;
    e.afterCustomStamp = session.customStamp;
    d->history.resize(d->historyPtr + 1);
    d->history.push_back(std::move(e));
    d->historyPtr = d->history.size() - 1;
    pruneHistory(d);
    ++d->historyRevision;
    markDirty();
    emit historyChanged();
}

void Editor::pushRegionHistory(const QVector<RegionHistoryChange>& changes, bool beforeAuthored,
                               const QString& label)
{
    MapDoc* d = doc();
    if (!d) return;
    const bool afterAuthored = d->rpgMakerRegionsAuthored;
    if (changes.isEmpty() && beforeAuthored == afterAuthored) return;

    HistoryEntry e;
    e.regionDiff = true;
    e.label = label.isEmpty() ? QStringLiteral("Editar regiões RPG Maker") : label;
    e.regionChanges = changes;
    e.beforeRegionsAuthored = beforeAuthored;
    e.afterRegionsAuthored = afterAuthored;
    d->history.resize(d->historyPtr + 1);
    d->history.push_back(std::move(e));
    d->historyPtr = d->history.size() - 1;
    pruneHistory(d);
    ++d->historyRevision;
    markDirty();
    emit historyChanged();
    emit mapChanged();
}

bool Editor::canUndo() const { const MapDoc* d = doc(); return d && d->historyPtr >= 0; }
bool Editor::canRedo() const { const MapDoc* d = doc(); return d && d->historyPtr < d->history.size() - 1; }

void Editor::undo()
{
    MapDoc* d = doc();
    if (!d || d->historyPtr < 0) return;
    const HistoryEntry& e = d->history[d->historyPtr];
    if (e.document) {
        if (e.tilesetSnapshot) {
            tilesets = e.beforeTilesets;
            session.activeTilesetIdx = e.beforeActiveTilesetIdx;
            session.tsSel = e.beforeTsSel;
            session.customStamp = e.beforeCustomStamp;
            reindexTilesetGids();
        }
        applyDocSnapshot(e.beforeDoc);
    } else if (e.regionDiff) {
        for (const RegionHistoryChange& change : e.regionChanges) {
            const quint64 key = MapDoc::regionKey(change.x, change.y);
            if (change.before <= 0) d->rpgMakerRegions.remove(key);
            else if (d->regionInBounds(change.x, change.y))
                d->rpgMakerRegions.insert(key, quint8(qBound(1, change.before, 255)));
        }
        d->rpgMakerRegionsAuthored = e.beforeRegionsAuthored;
    } else {
        LayerPtr l = findNode(e.layerId);
        if (l && e.rasterDiff) {
            QImage* target = e.rasterMaskDiff ? &l->imageMask : &l->image;
            if (target && !target->isNull()) {
                QPainter painter(target);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(e.rasterRect.topLeft(), e.beforeRaster);
                painter.end();
                if (!e.rasterMaskDiff && l->type == LayerType::Image) {
                    l->imagewidth = l->image.width();
                    l->imageheight = l->image.height();
                }
            }
        } else if (l && e.tileDiff && l->type == LayerType::Tile) {
            for (const TileHistoryChange& change : e.tileChanges)
                if (l->inBounds(change.x, change.y)) l->data2D[change.y][change.x] = change.before;
        } else if (l) {
            applySnapshot(l, e.beforeLayer);
        }
    }
    --d->historyPtr;
    ++d->historyRevision;
    markDirty();
    if (e.document && e.tilesetSnapshot) emit tilesetsChanged();
    emit historyChanged();
    emit layersChanged();
    emit mapChanged();
}

void Editor::redo()
{
    MapDoc* d = doc();
    if (!d || d->historyPtr >= d->history.size() - 1) return;
    ++d->historyPtr;
    const HistoryEntry& e = d->history[d->historyPtr];
    if (e.document) {
        if (e.tilesetSnapshot) {
            tilesets = e.afterTilesets;
            session.activeTilesetIdx = e.afterActiveTilesetIdx;
            session.tsSel = e.afterTsSel;
            session.customStamp = e.afterCustomStamp;
            reindexTilesetGids();
        }
        applyDocSnapshot(e.afterDoc);
    } else if (e.regionDiff) {
        for (const RegionHistoryChange& change : e.regionChanges) {
            const quint64 key = MapDoc::regionKey(change.x, change.y);
            if (change.after <= 0) d->rpgMakerRegions.remove(key);
            else if (d->regionInBounds(change.x, change.y))
                d->rpgMakerRegions.insert(key, quint8(qBound(1, change.after, 255)));
        }
        d->rpgMakerRegionsAuthored = e.afterRegionsAuthored;
    } else {
        LayerPtr l = findNode(e.layerId);
        if (l && e.rasterDiff) {
            QImage* target = e.rasterMaskDiff ? &l->imageMask : &l->image;
            if (target && !target->isNull()) {
                QPainter painter(target);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(e.rasterRect.topLeft(), e.afterRaster);
                painter.end();
                if (!e.rasterMaskDiff && l->type == LayerType::Image) {
                    l->imagewidth = l->image.width();
                    l->imageheight = l->image.height();
                }
            }
        } else if (l && e.tileDiff && l->type == LayerType::Tile) {
            for (const TileHistoryChange& change : e.tileChanges)
                if (l->inBounds(change.x, change.y)) l->data2D[change.y][change.x] = change.after;
        } else if (l) {
            applySnapshot(l, e.afterLayer);
        }
    }
    ++d->historyRevision;
    markDirty();
    if (e.document && e.tilesetSnapshot) emit tilesetsChanged();
    emit historyChanged();
    emit layersChanged();
    emit mapChanged();
}

QStringList Editor::historyLabels(int* currentPtr) const
{
    QStringList out;
    const MapDoc* d = doc();
    if (!d) { if (currentPtr) *currentPtr = -1; return out; }
    for (const HistoryEntry& e : d->history) out << e.label;
    if (currentPtr) *currentPtr = d->historyPtr;
    return out;
}

void Editor::jumpHistory(int index)
{
    MapDoc* d = doc();
    if (!d) return;
    index = clampi(index, -1, d->history.size() - 1);
    while (d->historyPtr > index) undo();
    while (d->historyPtr < index) redo();
}


} // namespace core
