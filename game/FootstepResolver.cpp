#include "FootstepResolver.h"

#include "core/Editor.h"
#include "core/Model.h"
#include "game/GameState.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QtMath>
#include <algorithm>

namespace game {
namespace {

void collectTileLayers(const QVector<core::LayerPtr>& roots, QVector<core::LayerPtr>* out,
                       bool parentVisible = true)
{
    if (!out) return;
    for (const core::LayerPtr& layer : roots) {
        if (!layer) continue;
        const bool visible = parentVisible && layer->visible;
        if (!visible) continue;
        if (layer->type == core::LayerType::Tile) out->push_back(layer);
        if (!layer->children.isEmpty()) collectTileLayers(layer->children, out, visible);
    }
}

QString wangSurfaceForTile(const core::Editor& ed, const core::TileRef& tile)
{
    auto surfaceFrom = [&](const core::WangSet& ws, int colorId) -> QString {
        if (colorId < 0) return {};
        if (const core::WangColor* color = ws.colorById(colorId))
            if (!color->footstepSurfaceId.trimmed().isEmpty()) return color->footstepSurfaceId.trimmed();
        return {};
    };

    if (!tile.wangSetId.isEmpty()) {
        for (const core::WangSet& ws : ed.wangSets) {
            if (ws.id != tile.wangSetId) continue;
            const QString direct = surfaceFrom(ws, tile.wangColorId);
            if (!direct.isEmpty()) return direct;
            const auto it = ws.tiles.constFind(core::WangSet::tileKeyOf(tile.tilesetIdx, tile.tx, tile.ty));
            if (it != ws.tiles.cend()) {
                const core::WangTileData& wd = it.value();
                if (wd.isolatedColorId >= 0) return surfaceFrom(ws, wd.isolatedColorId);
                const int values[] = {wd.b, wd.bl, wd.br, wd.l, wd.r, wd.t, wd.tl, wd.tr};
                for (int cid : values) { const QString s = surfaceFrom(ws, cid); if (!s.isEmpty()) return s; }
            }
            break;
        }
    }

    const QString key = core::WangSet::tileKeyOf(tile.tilesetIdx, tile.tx, tile.ty);
    for (const core::WangSet& ws : ed.wangSets) {
        const auto it = ws.tiles.constFind(key);
        if (it == ws.tiles.cend()) continue;
        const core::WangTileData& wd = it.value();
        if (wd.isolatedColorId >= 0) {
            const QString s = surfaceFrom(ws, wd.isolatedColorId);
            if (!s.isEmpty()) return s;
        }
        const int values[] = {wd.b, wd.bl, wd.br, wd.l, wd.r, wd.t, wd.tl, wd.tr};
        for (int cid : values) { const QString s = surfaceFrom(ws, cid); if (!s.isEmpty()) return s; }
    }
    return {};
}

QString soundPath(const core::Editor& ed, const core::FootstepSound& sound)
{
    QString rel;
    if (!sound.assetId.trimmed().isEmpty()) rel = ed.assetDatabase.pathForId(sound.assetId.trimmed());
    if (rel.trimmed().isEmpty()) rel = sound.sourcePath.trimmed();
    if (rel.isEmpty()) return {};
    return QFileInfo(rel).isAbsolute() ? QDir::cleanPath(rel)
                                       : QDir::cleanPath(QDir(ed.projectRoot()).filePath(rel));
}

quint64 mixHash(quint64 x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

} // namespace

QString FootstepResolver::groundSurfaceId(const core::Editor& ed, const GameState& state,
                                          const QString& mapId, const QPointF& actorCell)
{
    const core::MapDoc* map = ed.mapById(mapId);
    if (!map) return {};
    const int baseW = qMax(1, map->map.tileWidth);
    const int baseH = qMax(1, map->map.tileHeight);
    const double px = (actorCell.x() + 0.5) * baseW;
    const double py = (actorCell.y() + 0.95) * baseH;
    const int baseX = qFloor(px / baseW);
    const int baseY = qFloor(py / baseH);

    // Override de Terrain/Tag runtime: é um dado de gameplay explícito e vence
    // o terrain inferido por Wang. Tile explícito ainda é a regra mais específica.
    const int runtimeTerrain = state.runtimeMapTerrain(mapId, baseX, baseY, 0);

    QVector<core::LayerPtr> layers;
    collectTileLayers(map->layers, &layers);
    QString wangFallback;
    for (auto layerIt = layers.crbegin(); layerIt != layers.crend(); ++layerIt) {
        const core::LayerPtr& layer = *layerIt;
        if (!layer || layer->zMode.compare(QStringLiteral("above"), Qt::CaseInsensitive) == 0) continue;
        const int tw = qMax(1, layer->tileWidth), th = qMax(1, layer->tileHeight);
        const int lx = qFloor((px - layer->offsetx) / tw);
        const int ly = qFloor((py - layer->offsety) / th);
        if (!layer->inBounds(lx, ly)) continue;
        const core::Cell stack = state.runtimeMapCell(ed, mapId, layer->id, lx, ly);
        for (auto tileIt = stack.crbegin(); tileIt != stack.crend(); ++tileIt) {
            const core::TileRef& tile = *tileIt;
            const core::Tileset* ts = ed.tilesetAt(tile.tilesetIdx);
            if (!ts || !ts->contains(tile.tx, tile.ty)) continue;
            const QString explicitSurface = ed.tileFootstepSurface(tile.tilesetIdx, tile.tx, tile.ty);
            if (!explicitSurface.isEmpty() && ed.footstepSurfaceById(explicitSurface)) return explicitSurface;
            if (wangFallback.isEmpty()) {
                const QString inferred = wangSurfaceForTile(ed, tile);
                if (!inferred.isEmpty() && ed.footstepSurfaceById(inferred)) wangFallback = inferred;
            }
        }
    }

    const QString runtimeSurface = ed.footstepSettings.terrainSurfaceIds.value(runtimeTerrain);
    if (runtimeTerrain != 0 && !runtimeSurface.isEmpty() && ed.footstepSurfaceById(runtimeSurface)) return runtimeSurface;
    if (!wangFallback.isEmpty()) return wangFallback;

    // Tag 0 também pode ser configurado conscientemente como terreno base.
    const QString baseTerrainSurface = ed.footstepSettings.terrainSurfaceIds.value(runtimeTerrain);
    if (!baseTerrainSurface.isEmpty() && ed.footstepSurfaceById(baseTerrainSurface)) return baseTerrainSurface;
    if (ed.footstepSurfaceById(ed.footstepSettings.defaultSurfaceId)) return ed.footstepSettings.defaultSurfaceId;
    return {};
}

QString FootstepResolver::actorSurfaceId(const core::Editor& ed, const GameState& state,
                                         const QString& mapId, const QString& eventId,
                                         const QPointF& actorCell, int* actorVolume)
{
    int volume = 100;
    QString overrideId;
    if (eventId.isEmpty()) {
        if (!ed.player.footstepsEnabled) { if (actorVolume) *actorVolume = 0; return {}; }
        overrideId = ed.player.footstepSurfaceId.trimmed();
        volume = qBound(0, ed.player.footstepVolume, 100);
    } else {
        const core::MapDoc* map = ed.mapById(mapId);
        const core::MapEvent* found = nullptr;
        if (map) for (const core::MapEvent& event : map->events) if (event.id == eventId) { found = &event; break; }
        if (found) {
            const int pageIndex = state.choosePage(*found);
            if (pageIndex >= 0 && pageIndex < found->pages.size()) {
                const core::EventPage& page = found->pages.at(pageIndex);
                if (!page.footstepsEnabled) { if (actorVolume) *actorVolume = 0; return {}; }
                overrideId = page.footstepSurfaceId.trimmed();
                volume = qBound(0, page.footstepVolume, 100);
            }
        }
    }
    if (actorVolume) *actorVolume = volume;
    if (!overrideId.isEmpty() && ed.footstepSurfaceById(overrideId)) return overrideId;
    return groundSurfaceId(ed, state, mapId, actorCell);
}

FootstepDecision FootstepResolver::resolve(const core::Editor& ed, const GameState& state,
                                           const QString& mapId, const QString& eventId,
                                           const QPointF& actorCell,
                                           const QString& forcedSurfaceId,
                                           int commandVolume, quint64 serial,
                                           int lastVariant, int previousVariant)
{
    FootstepDecision out;
    int actorVolume = 100;
    QString surfaceId = forcedSurfaceId.trimmed();
    if (!surfaceId.isEmpty()) {
        // Comando explícito ainda respeita o volume do ator, mas permite testar
        // um material específico sem alterar os dados do mapa.
        (void)actorSurfaceId(ed, state, mapId, eventId, actorCell, &actorVolume);
    } else {
        surfaceId = actorSurfaceId(ed, state, mapId, eventId, actorCell, &actorVolume);
    }
    const core::FootstepSurface* surface = ed.footstepSurfaceById(surfaceId);
    if (!surface) return out;

    struct Candidate { int index; QString path; int weight; };
    QVector<Candidate> candidates;
    for (int i = 0; i < surface->sounds.size(); ++i) {
        const core::FootstepSound& sound = surface->sounds.at(i);
        const QString path = soundPath(ed, sound);
        if (path.isEmpty()) continue;
        const int weight = qMax(1, sound.weight);
        candidates.push_back({i, path, weight});
    }
    if (candidates.isEmpty()) return out;

    const quint64 seed = mixHash(serial ^ quint64(qHash(surfaceId)) ^ (quint64(qHash(eventId)) << 1));

    // RC2.58: seleção ponderada com memória curta. O algoritmo antigo sorteava
    // primeiro e, em caso de repetição, pulava por índice — isso distorcia pesos
    // e podia formar ABAB/ABCABC perceptível. Agora o último sample é removido
    // do sorteio quando solicitado e o penúltimo recebe apenas uma penalidade
    // suave (quando há 3+ variações), preservando aleatoriedade e pesos.
    QVector<int> effectiveWeights;
    effectiveWeights.reserve(candidates.size());
    int effectiveTotal = 0;
    for (const Candidate& candidate : candidates) {
        int weight = candidate.weight;
        if (surface->avoidImmediateRepeat && candidates.size() > 1 &&
            candidate.index == lastVariant) {
            weight = 0;
        } else if (candidates.size() > 2 && candidate.index == previousVariant) {
            weight = qMax(1, weight / 3);
        }
        effectiveWeights.push_back(weight);
        effectiveTotal += weight;
    }
    if (effectiveTotal <= 0) {
        effectiveWeights.fill(1);
        effectiveTotal = candidates.size();
    }
    int pickWeight = int(seed % quint64(effectiveTotal));
    int chosenCandidate = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const int weight = effectiveWeights.at(i);
        if (weight <= 0) continue;
        if (pickWeight < weight) { chosenCandidate = i; break; }
        pickWeight -= weight;
    }

    const int minPitch = qBound(50, qMin(surface->pitchMin, surface->pitchMax), 200);
    const int maxPitch = qBound(50, qMax(surface->pitchMin, surface->pitchMax), 200);
    const int pitchSpan = maxPitch - minPitch + 1;
    out.surfaceId = surfaceId;
    out.sourcePath = candidates[chosenCandidate].path;
    out.variantIndex = candidates[chosenCandidate].index;
    const quint64 pitchSeed = mixHash(seed ^ 0xd6e8feb86659fd93ULL);
    out.pitch = minPitch + int(pitchSeed % quint64(qMax(1, pitchSpan)));
    out.volume = qBound(0, qRound(surface->volume * (actorVolume / 100.0) * (qBound(0, commandVolume, 100) / 100.0)), 100);
    return out;
}

} // namespace game
