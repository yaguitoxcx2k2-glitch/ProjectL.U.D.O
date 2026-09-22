#include "ProjectReferenceIndex.h"

#include "Editor.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>

namespace core {
namespace {

ProjectReferenceLocation mapLocation(const MapDoc& map, const QString& detail = QString())
{
    ProjectReferenceLocation location;
    location.ownerType = QStringLiteral("map");
    location.ownerId = map.id;
    location.ownerName = map.name;
    location.mapId = map.id;
    location.detail = detail;
    return location;
}

void collectLayerUsages(const Editor& ed, const MapDoc& map, const LayerPtr& layer,
                        QVector<ProjectReferenceUsage>& out,
                        QSet<QString>& seenTilesets)
{
    if (!layer) return;

    auto addTileset = [&](int index, const QString& detail) {
        const Tileset* ts = ed.tilesetAt(index);
        if (!ts || ts->id.isEmpty()) return;
        const QString key = map.id + QLatin1Char('|') + ts->id;
        if (seenTilesets.contains(key)) return;
        seenTilesets.insert(key);
        ProjectReferenceUsage usage;
        usage.kind = ReferenceSymbolKind::Tileset;
        usage.symbolId = ts->id;
        usage.location = mapLocation(map, detail);
        out.push_back(usage);
    };

    if (layer->type == LayerType::Tile) {
        for (const QVector<Cell>& row : layer->data2D)
            for (const Cell& cell : row)
                for (const TileRef& tile : cell)
                    addTileset(tile.tilesetIdx, QStringLiteral("camada: ") + layer->name);
    }

    for (const MapObject& object : layer->objects) {
        for (const TileRef& tile : object.tiles)
            addTileset(tile.tilesetIdx, QStringLiteral("objeto: ") + object.name);

        // Propriedades de objetos podem apontar para outro mapa por ID estável.
        // Apenas igualdade exata conta como dependência semântica.
        for (auto it = object.properties.cbegin(); it != object.properties.cend(); ++it) {
            if (!ed.mapById(it.value())) continue;
            ProjectReferenceUsage usage;
            usage.kind = ReferenceSymbolKind::Map;
            usage.symbolId = it.value();
            usage.location = mapLocation(
                map, QStringLiteral("propriedade %1 do objeto %2").arg(it.key(), object.name));
            out.push_back(usage);
        }
    }

    for (const LayerPtr& child : layer->children)
        collectLayerUsages(ed, map, child, out, seenTilesets);
}

bool symbolExists(const QVector<ProjectReferenceSymbol>& symbols,
                  ReferenceSymbolKind kind, const QString& id)
{
    for (const ProjectReferenceSymbol& symbol : symbols)
        if (symbol.kind == kind && symbol.id == id) return true;
    return false;
}

} // namespace

QString referenceSymbolKindId(ReferenceSymbolKind kind)
{
    switch (kind) {
    case ReferenceSymbolKind::Map: return QStringLiteral("map");
    case ReferenceSymbolKind::Tileset: return QStringLiteral("tileset");
    }
    return QStringLiteral("unknown");
}

QString referenceSymbolKindLabel(ReferenceSymbolKind kind)
{
    switch (kind) {
    case ReferenceSymbolKind::Map: return QStringLiteral("Mapa");
    case ReferenceSymbolKind::Tileset: return QStringLiteral("Tileset");
    }
    return QStringLiteral("Referência");
}

QVector<ProjectReferenceSymbol> projectReferenceSymbols(const Editor& ed)
{
    QVector<ProjectReferenceSymbol> out;
    out.reserve(ed.docs.size() + ed.tilesets.size());

    for (const MapDoc& map : ed.docs) {
        ProjectReferenceSymbol symbol;
        symbol.kind = ReferenceSymbolKind::Map;
        symbol.id = map.id;
        symbol.parentId = map.parentId;
        symbol.name = map.name;
        symbol.qualifiedName = map.name;
        out.push_back(symbol);
    }

    for (const Tileset& ts : ed.tilesets) {
        ProjectReferenceSymbol symbol;
        symbol.kind = ReferenceSymbolKind::Tileset;
        symbol.id = ts.id;
        symbol.name = ts.name;
        symbol.qualifiedName = ts.name;
        out.push_back(symbol);
    }
    return out;
}

QVector<ProjectReferenceUsage> projectReferenceUsages(const Editor& ed)
{
    QVector<ProjectReferenceUsage> out;
    QSet<QString> seenTilesets;

    for (const MapDoc& map : ed.docs) {
        if (!map.parentId.isEmpty()) {
            ProjectReferenceUsage usage;
            usage.kind = ReferenceSymbolKind::Map;
            usage.symbolId = map.parentId;
            usage.location = mapLocation(map, QStringLiteral("mapa pai"));
            out.push_back(usage);
        }

        for (const LayerPtr& layer : map.layers)
            collectLayerUsages(ed, map, layer, out, seenTilesets);
    }

    // O pool aleatório é global ao projeto e mantém TileRef por índice.
    QSet<QString> poolTilesets;
    for (const RandomEntry& entry : ed.randomPool) {
        const Tileset* ts = ed.tilesetAt(entry.tilesetIdx);
        if (!ts || ts->id.isEmpty() || poolTilesets.contains(ts->id)) continue;
        poolTilesets.insert(ts->id);

        ProjectReferenceUsage usage;
        usage.kind = ReferenceSymbolKind::Tileset;
        usage.symbolId = ts->id;
        usage.location.ownerType = QStringLiteral("project");
        usage.location.ownerId = QStringLiteral("randomPool");
        usage.location.ownerName = QStringLiteral("Pool aleatório");
        usage.location.detail = QStringLiteral("pool aleatório");
        out.push_back(usage);
    }

    return out;
}

QVector<ProjectTextOccurrence> projectTextOccurrences(const Editor& ed)
{
    QVector<ProjectTextOccurrence> out;
    for (const MapDoc& map : ed.docs) {
        if (!map.name.trimmed().isEmpty())
            out.push_back({mapLocation(map, QStringLiteral("nome do mapa")), map.name});

        std::function<void(const LayerPtr&)> visit = [&](const LayerPtr& layer) {
            if (!layer) return;
            if (!layer->name.trimmed().isEmpty())
                out.push_back({mapLocation(map, QStringLiteral("camada")), layer->name});
            for (const MapObject& object : layer->objects) {
                if (!object.name.trimmed().isEmpty())
                    out.push_back({mapLocation(map, QStringLiteral("objeto")), object.name});
                for (auto it = object.properties.cbegin(); it != object.properties.cend(); ++it)
                    if (!it.value().trimmed().isEmpty())
                        out.push_back({mapLocation(map, QStringLiteral("propriedade %1").arg(it.key())),
                                       it.value()});
            }
            for (const LayerPtr& child : layer->children) visit(child);
        };
        for (const LayerPtr& layer : map.layers) visit(layer);
    }
    return out;
}

QVector<ProjectReferenceUsage> findProjectUses(const Editor& ed, ReferenceSymbolKind kind,
                                               const QString& symbolId)
{
    QVector<ProjectReferenceUsage> out;
    for (const ProjectReferenceUsage& usage : projectReferenceUsages(ed))
        if (usage.kind == kind && usage.symbolId == symbolId) out.push_back(usage);
    return out;
}

ProjectReferenceLocation projectDefinitionLocation(const Editor& ed,
                                                    ReferenceSymbolKind kind,
                                                    const QString& symbolId,
                                                    const QString&)
{
    if (kind == ReferenceSymbolKind::Map) {
        if (const MapDoc* map = ed.mapById(symbolId))
            return mapLocation(*map, QStringLiteral("definição"));
    }
    if (kind == ReferenceSymbolKind::Tileset) {
        for (const Tileset& ts : ed.tilesets) {
            if (ts.id != symbolId) continue;
            ProjectReferenceLocation location;
            location.ownerType = QStringLiteral("tileset");
            location.ownerId = ts.id;
            location.ownerName = ts.name;
            location.detail = QStringLiteral("definição");
            return location;
        }
    }
    return {};
}

QVector<ProjectReferenceSymbol> unusedSymbols(const Editor& ed)
{
    const QVector<ProjectReferenceSymbol> symbols = projectReferenceSymbols(ed);
    const QVector<ProjectReferenceUsage> usages = projectReferenceUsages(ed);
    QVector<ProjectReferenceSymbol> out;

    for (const ProjectReferenceSymbol& symbol : symbols) {
        // Mapas raiz podem legitimamente não possuir referências de entrada.
        if (symbol.kind == ReferenceSymbolKind::Map) continue;
        bool used = false;
        for (const ProjectReferenceUsage& usage : usages)
            if (usage.kind == symbol.kind && usage.symbolId == symbol.id) {
                used = true;
                break;
            }
        if (!used) out.push_back(symbol);
    }
    return out;
}

QVector<ProjectReferenceUsage> orphanedReferences(const Editor& ed)
{
    const QVector<ProjectReferenceSymbol> symbols = projectReferenceSymbols(ed);
    QVector<ProjectReferenceUsage> out;
    for (const ProjectReferenceUsage& usage : projectReferenceUsages(ed))
        if (!symbolExists(symbols, usage.kind, usage.symbolId)) out.push_back(usage);
    return out;
}

QVector<QStringList> circularReferences(const Editor& ed)
{
    QVector<QStringList> out;
    QSet<QString> emitted;

    for (const MapDoc& start : ed.docs) {
        QStringList chain;
        QHash<QString, int> index;
        QString current = start.id;
        while (!current.isEmpty()) {
            if (index.contains(current)) {
                const QStringList cycle = chain.mid(index.value(current));
                QStringList canonical = cycle;
                std::sort(canonical.begin(), canonical.end());
                const QString key = canonical.join(QLatin1Char('|'));
                if (!emitted.contains(key)) {
                    emitted.insert(key);
                    QStringList tagged;
                    for (const QString& id : cycle)
                        tagged.push_back(QStringLiteral("map:") + id);
                    out.push_back(tagged);
                }
                break;
            }
            index.insert(current, chain.size());
            chain.push_back(current);
            const MapDoc* map = ed.mapById(current);
            if (!map) break;
            current = map->parentId;
        }
    }
    return out;
}

bool renameProjectSymbol(Editor& ed, ReferenceSymbolKind kind, const QString& symbolId,
                         const QString& newName, QString* error)
{
    const QString clean = newName.trimmed();
    if (clean.isEmpty()) {
        if (error) *error = QStringLiteral("O nome não pode ficar vazio.");
        return false;
    }

    if (kind == ReferenceSymbolKind::Map) {
        MapDoc* map = ed.mapById(symbolId);
        if (!map) {
            if (error) *error = QStringLiteral("Mapa inexistente.");
            return false;
        }
        if (map->name == clean) return true;
        map->name = clean;
        map->dirty = true;
        ed.markDirty();
        emit ed.projectChanged();
        emit ed.mapChanged();
        return true;
    }

    if (kind == ReferenceSymbolKind::Tileset) {
        for (Tileset& ts : ed.tilesets) {
            if (ts.id != symbolId) continue;
            if (ts.name == clean) return true;
            ts.name = clean;
            ed.markDirty();
            emit ed.tilesetsChanged();
            return true;
        }
        if (error) *error = QStringLiteral("Tileset inexistente.");
        return false;
    }

    if (error) *error = QStringLiteral("Tipo de símbolo inválido para o Map Editor.");
    return false;
}

} // namespace core
