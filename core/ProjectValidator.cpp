#include "ProjectValidator.h"

#include "MapWorkflow.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSet>

namespace core {
namespace {

void addIssue(ProjectValidationResult& result, ValidationSeverity severity,
              const QString& code, const QString& location,
              const QString& message, bool safelyFixable = false)
{
    result.issues.push_back({severity, code, location, message, safelyFixable});
    if (severity == ValidationSeverity::Error) ++result.errorCount;
    else if (severity == ValidationSeverity::Warning) ++result.warningCount;
    else ++result.infoCount;
}

void validateLayer(const Editor& ed, const MapDoc& map, const LayerPtr& layer,
                   const QString& parentLocation, QSet<QString>& layerIds,
                   ProjectValidationResult& result)
{
    if (!layer) {
        addIssue(result, ValidationSeverity::Error, QStringLiteral("layer.null"),
                 parentLocation, QObject::tr("Há uma camada inválida no mapa."));
        return;
    }

    const QString location = QObject::tr("%1 / camada “%2”").arg(parentLocation, layer->name);
    if (layer->id.isEmpty() || layerIds.contains(layer->id))
        addIssue(result, ValidationSeverity::Error, QStringLiteral("layer.id"), location,
                 QObject::tr("A camada tem ID vazio ou duplicado."));
    else
        layerIds.insert(layer->id);

    if (layer->type == LayerType::Tile) {
        if (layer->tileWidth <= 0 || layer->tileHeight <= 0) {
            addIssue(result, ValidationSeverity::Error, QStringLiteral("layer.grid.size"), location,
                     QObject::tr("A grade da camada possui tile de tamanho inválido."));
        } else {
            const int expectedCols =
                (map.map.pixelWidth() + layer->tileWidth - 1) / layer->tileWidth;
            const int expectedRows =
                (map.map.pixelHeight() + layer->tileHeight - 1) / layer->tileHeight;
            if (layer->cols != expectedCols || layer->rows != expectedRows ||
                layer->data2D.size() != layer->rows) {
                addIssue(result, ValidationSeverity::Error, QStringLiteral("layer.grid.extent"),
                         location,
                         QObject::tr("A grade interna não acompanha o tamanho atual do mapa."),
                         true);
            } else {
                for (const QVector<Cell>& row : layer->data2D) {
                    if (row.size() != layer->cols) {
                        addIssue(result, ValidationSeverity::Error,
                                 QStringLiteral("layer.grid.row"), location,
                                 QObject::tr("Uma linha da grade possui largura incorreta."), true);
                        break;
                    }
                }
            }
        }

        int brokenTiles = 0;
        int brokenWangRefs = 0;
        for (const QVector<Cell>& row : layer->data2D) {
            for (const Cell& cell : row) {
                for (const TileRef& tile : cell) {
                    const Tileset* tileset = ed.tilesetAt(tile.tilesetIdx);
                    if (!tileset || !tileset->contains(tile.tx, tile.ty))
                        ++brokenTiles;

                    if (tile.wangSetId.isEmpty()) {
                        if (tile.wangColorId >= 0) ++brokenWangRefs;
                        continue;
                    }
                    const WangSet* set = nullptr;
                    for (const WangSet& candidate : ed.wangSets) {
                        if (candidate.id == tile.wangSetId) {
                            set = &candidate;
                            break;
                        }
                    }
                    if (!set || !set->colorById(tile.wangColorId))
                        ++brokenWangRefs;
                }
            }
        }

        if (brokenTiles > 0)
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("layer.tile.reference"), location,
                     QObject::tr("%1 referência(s) de tile apontam para um tileset ou célula inexistente.")
                         .arg(brokenTiles));

        if (brokenWangRefs > 0)
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("layer.wang.reference"), location,
                     QObject::tr("%1 referência(s) Wang apontam para Set/cor inexistente.")
                         .arg(brokenWangRefs));
    }

    if (layer->type == LayerType::Image) {
        if (!layer->imageReferenceOnly && layer->image.isNull()) {
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("layer.image.baked.empty"), location,
                     QObject::tr("A camada de imagem rasterizada está vazia."));
        } else if (!layer->imagePath.trimmed().isEmpty()) {
            const QFileInfo info(layer->imagePath);
            const QString absolute = info.isAbsolute()
                                         ? info.absoluteFilePath()
                                         : QDir(ed.projectRoot()).filePath(layer->imagePath);
            if (!QFileInfo::exists(absolute) && layer->image.isNull())
                addIssue(result, ValidationSeverity::Warning,
                         QStringLiteral("layer.image.missing"), location,
                         QObject::tr("A imagem desta camada não foi encontrada."));
        }
    }

    for (const LayerPtr& child : layer->children)
        validateLayer(ed, map, child, location, layerIds, result);
}

void repairLayer(const MapDoc& map, const LayerPtr& layer,
                 int& changed, QSet<quintptr>& visited)
{
    if (!layer) return;
    const quintptr key = quintptr(layer.data());
    if (visited.contains(key)) return;
    visited.insert(key);

    if (layer->type == LayerType::Tile && layer->tileWidth > 0 && layer->tileHeight > 0) {
        const int cols =
            (map.map.pixelWidth() + layer->tileWidth - 1) / layer->tileWidth;
        const int rows =
            (map.map.pixelHeight() + layer->tileHeight - 1) / layer->tileHeight;
        bool malformed =
            layer->cols != cols || layer->rows != rows || layer->data2D.size() != layer->rows;
        if (!malformed) {
            for (const QVector<Cell>& row : layer->data2D) {
                if (row.size() != layer->cols) {
                    malformed = true;
                    break;
                }
            }
        }
        if (malformed) {
            layer->resizeGrid(cols, rows);
            ++changed;
        }
    }

    for (const LayerPtr& child : layer->children)
        repairLayer(map, child, changed, visited);
}

} // namespace

QString validationSeverityLabel(ValidationSeverity severity)
{
    if (severity == ValidationSeverity::Error) return QObject::tr("Erro");
    if (severity == ValidationSeverity::Warning) return QObject::tr("Aviso");
    return QObject::tr("Informação");
}

ProjectValidationResult ProjectValidator::validate(const Editor& ed)
{
    ProjectValidationResult result;

    if (ed.docs.isEmpty()) {
        addIssue(result, ValidationSeverity::Error,
                 QStringLiteral("project.maps.empty"), QObject::tr("Projeto"),
                 QObject::tr("O projeto não possui nenhum mapa."));
        return result;
    }

    if (ed.projectId.trimmed().isEmpty())
        addIssue(result, ValidationSeverity::Warning,
                 QStringLiteral("project.id.empty"), QObject::tr("Projeto"),
                 QObject::tr("A identidade do projeto está vazia."));

    for (const AssetRecord& asset : ed.assetDatabase.missingRecords())
        addIssue(result, ValidationSeverity::Warning,
                 QStringLiteral("asset.database.missing"),
                 QObject::tr("Assets / %1").arg(asset.path),
                 QObject::tr("Asset ausente no Asset Database (ID %1). Use ‘Localizar ausentes…’ no navegador de Assets.")
                     .arg(asset.id));

    QSet<QString> tilesetIds;
    for (const Tileset& ts : ed.tilesets) {
        const QString location = QObject::tr("Tileset “%1”").arg(ts.name);
        if (ts.id.trimmed().isEmpty() || tilesetIds.contains(ts.id))
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("tileset.id"), location,
                     QObject::tr("O tileset possui ID vazio ou duplicado."));
        else
            tilesetIds.insert(ts.id);

        if (ts.tilewidth <= 0 || ts.tileheight <= 0 ||
            ts.columns < 0 || ts.rows < 0 || ts.tilecount < 0)
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("tileset.grid"), location,
                     QObject::tr("A grade do tileset possui dimensões inválidas."));

        for (auto it = ts.tilePriorities.cbegin(); it != ts.tilePriorities.cend(); ++it) {
            if (it.value() < 0 || it.value() > 5) {
                addIssue(result, ValidationSeverity::Error,
                         QStringLiteral("tileset.priority"), location,
                         QObject::tr("Há prioridade de tile fora do intervalo 0–5."));
                break;
            }
        }

        for (auto it = ts.tileCollisionMasks.cbegin();
             it != ts.tileCollisionMasks.cend(); ++it) {
            if (it.value() < 0 || it.value() > 15) {
                addIssue(result, ValidationSeverity::Error,
                         QStringLiteral("tileset.collision"), location,
                         QObject::tr("Há máscara de colisão de tile inválida."));
                break;
            }
        }

        for (const AnimatedAutotile& animation : ts.animatedAutotiles) {
            if (!animation.valid()) {
                addIssue(result, ValidationSeverity::Warning,
                         QStringLiteral("tileset.animation"), location,
                         QObject::tr("Há um tileset animado com região ou frames inválidos."));
                break;
            }
        }
    }

    for (const TilesetAutotile& autotile : ed.autotiles) {
        bool ownerExists = false;
        for (const Tileset& ts : ed.tilesets)
            if (ts.id == autotile.tilesetId) {
                ownerExists = true;
                break;
            }
        if (!ownerExists)
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("autotile.tileset"),
                     QObject::tr("Autotile “%1”").arg(autotile.name),
                     QObject::tr("O tileset físico associado ao Autotile não existe."));
    }

    QSet<QString> mapIds;
    QSet<QString> layerIds;
    for (const MapDoc& map : ed.docs) {
        const QString location = QObject::tr("Mapa “%1”").arg(map.name);

        if (map.id.isEmpty() || mapIds.contains(map.id))
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("map.id"), location,
                     QObject::tr("O mapa tem ID vazio ou duplicado."));
        else
            mapIds.insert(map.id);

        if (map.map.width <= 0 || map.map.height <= 0 ||
            map.map.tileWidth <= 0 || map.map.tileHeight <= 0)
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("map.size"), location,
                     QObject::tr("As dimensões do mapa ou dos tiles são inválidas."));

        for (auto it = map.rpgMakerRegions.cbegin(); it != map.rpgMakerRegions.cend(); ++it) {
            const int x = MapDoc::regionX(it.key()), y = MapDoc::regionY(it.key());
            if (!map.regionInBounds(x, y) || it.value() == 0) {
                addIssue(result, ValidationSeverity::Warning,
                         QStringLiteral("map.regions"), location,
                         QObject::tr("Há uma Região RPG Maker fora dos limites do mapa."), true);
                break;
            }
        }

        if (!map.parentId.isEmpty() && !ed.mapById(map.parentId))
            addIssue(result, ValidationSeverity::Warning,
                     QStringLiteral("map.parent.missing"), location,
                     QObject::tr("O mapa pai não existe."), true);
        else if (!map.parentId.isEmpty() &&
                 !mapworkflow::canReparent(ed, map.id, map.parentId))
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("map.parent.cycle"), location,
                     QObject::tr("A hierarquia de mapas contém um ciclo."), true);

        if (map.layers.isEmpty()) {
            addIssue(result, ValidationSeverity::Warning,
                     QStringLiteral("map.layers.empty"), location,
                     QObject::tr("O mapa não possui camadas."));
            continue;
        }

        const LayerTreeValidationResult layerTree = validateLayerTree(map.layers);
        if (!layerTree.ok) {
            addIssue(result, ValidationSeverity::Error,
                     QStringLiteral("layer.tree"), location,
                     QObject::tr("A hierarquia de camadas está inválida: %1")
                         .arg(layerTree.error));
            continue;
        }

        for (const LayerPtr& layer : map.layers)
            validateLayer(ed, map, layer, location, layerIds, result);
    }

    return result;
}

int ProjectValidator::repairSafe(Editor& ed, QStringList* changes)
{
    int changed = 0;

    const int hierarchyChanges = mapworkflow::normalizeHierarchy(ed);
    if (hierarchyChanges > 0) {
        changed += hierarchyChanges;
        if (changes)
            changes->push_back(
                QObject::tr("Hierarquia de mapas normalizada com segurança."));
    }

    for (MapDoc& map : ed.docs) {
        const int regionCountBefore = map.rpgMakerRegions.size();
        map.pruneRegions();
        if (map.rpgMakerRegions.size() != regionCountBefore) {
            ++changed;
            if (changes) changes->push_back(QObject::tr("Regiões RPG Maker fora do mapa foram removidas."));
        }

        const LayerTreeValidationResult layerTree = validateLayerTree(map.layers);
        if (!layerTree.ok) continue;

        QSet<quintptr> visitedLayers;
        for (const LayerPtr& layer : map.layers)
            repairLayer(map, layer, changed, visitedLayers);

        const QVector<LayerPtr> flat = flattenRenderableLayers(map.layers);
        if (flat.isEmpty()) {
            if (map.activeLayerIdx != -1 || !map.activeLayerId.isEmpty()) {
                map.activeLayerIdx = -1;
                map.activeLayerId.clear();
                ++changed;
            }
        } else {
            int resolved = qBound(0, map.activeLayerIdx, flat.size() - 1);
            if (!map.activeLayerId.isEmpty()) {
                for (int i = 0; i < flat.size(); ++i) {
                    if (flat[i] && flat[i]->id == map.activeLayerId) {
                        resolved = i;
                        break;
                    }
                }
            }
            const QString resolvedId =
                flat[resolved] ? flat[resolved]->id : QString();
            if (map.activeLayerIdx != resolved ||
                map.activeLayerId != resolvedId) {
                map.activeLayerIdx = resolved;
                map.activeLayerId = resolvedId;
                ++changed;
            }
        }
    }

    if (changed > 0) {
        ed.markDirty();
        emit ed.docsChanged();
        emit ed.layersChanged();
        emit ed.mapChanged();
    }
    return changed;
}

} // namespace core
