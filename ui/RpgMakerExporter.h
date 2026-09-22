// LUDO MAP EDITOR | Integração RPG Maker MV/MZ | versão do Editor em core/Version.h
// Header-only integration: no new translation unit or MOC registration.
#pragma once

#include "../core/Editor.h"
#include "../core/RpgMakerTarget.h"
#include "../core/LayerTree.h"
#include "../core/Renderer.h"
#include "../core/LayerRasterFilters.h"
#include "../core/ProjectIO.h"
#include "../core/TilesetOps.h"
#include "../core/TilesetCatalog.h"
#include "../core/Version.h"
#include "RpgMakerMvReset.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QVector>
#include <QVersionNumber>

#include <algorithm>
#include <cmath>
#include <exception>
#include <new>
#include <functional>

namespace ui::rpgMaker {

inline QString mapPrefix(int mapId)
{
    return QStringLiteral("Map%1").arg(mapId, 3, 10, QLatin1Char('0'));
}

inline QString mapJsonName(int mapId)
{
    return mapPrefix(mapId) + QStringLiteral(".json");
}

inline QString atlasName(int mapId, int tilesetIdx)
{
    return QStringLiteral("%1_tileset_%2.png").arg(mapPrefix(mapId)).arg(tilesetIdx);
}

inline QString referenceParallaxBaseName(int mapId)
{
    return QStringLiteral("LUDO_%1_REF").arg(mapPrefix(mapId));
}

inline QString referenceParallaxFileName(int mapId)
{
    return referenceParallaxBaseName(mapId) + QStringLiteral(".png");
}

inline bool writeAtomic(const QString& path, const QByteArray& data, QString& error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = QStringLiteral("Não foi possível gravar: ") + path;
        return false;
    }
    return true;
}

inline bool copyAtomic(const QString& source, const QString& target, QString& error)
{
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Não foi possível ler: ") + source;
        return false;
    }
    return writeAtomic(target, file.readAll(), error);
}

// Explicit manual export only. Autosave/sync never calls this helper.
inline bool exportSelectedPlugin(const QString& sourcePath, const QString& rootPath, QString& error,
                                 core::RpgMakerEngine engine = core::RpgMakerEngine::MZ)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Não foi possível ler o plugin selecionado: ") + sourcePath;
        return false;
    }
    const QByteArray bytes = source.readAll();
    const auto versionOf = [](const QByteArray& data) {
        const auto match = QRegularExpression(QStringLiteral("LudoMapSystem\\.js v([0-9]+\\.[0-9]+\\.[0-9]+)"))
            .match(QString::fromUtf8(data));
        return match.hasMatch() ? QVersionNumber::fromString(match.captured(1)) : QVersionNumber();
    };
    const auto sourceVersion = versionOf(bytes);
    const QByteArray expectedTarget = engine == core::RpgMakerEngine::MV ? QByteArrayLiteral("@target MV")
                                                                         : QByteArrayLiteral("@target MZ");
    if (bytes.isEmpty() || sourceVersion.isNull() || !bytes.contains(expectedTarget)) {
        error = QStringLiteral("O arquivo selecionado não é um LudoMapSystem.js válido para %1 com versão identificável.")
                    .arg(core::rpgMakerEngineName(engine));
        return false;
    }
    const QString target = QDir(rootPath).filePath(QStringLiteral("js/plugins/LudoMapSystem.js"));
    if (QFileInfo::exists(target)) {
        QFile existing(target);
        if (!existing.open(QIODevice::ReadOnly)) { error = QStringLiteral("Não foi possível ler o plugin instalado."); return false; }
        const QByteArray current = existing.readAll(); existing.close();
        const auto currentVersion = versionOf(current);
        if (currentVersion.isNull() || QVersionNumber::compare(sourceVersion,currentVersion) < 0) {
            error = QStringLiteral("O plugin instalado foi preservado: versão selecionada antiga ou versão instalada não identificável. Selecione o arquivo atualizado.");
            return false;
        }
        if (current == bytes) return true;
        if (!writeAtomic(target + QStringLiteral(".before-ludo-export.bak"),current,error)) return false;
    }
    if (!QDir(rootPath).mkpath(QStringLiteral("js/plugins"))) {
        error = QStringLiteral("Não foi possível criar js/plugins."); return false;
    }
    return writeAtomic(target,bytes,error);
}

inline bool exportSelectedReflectionPlugin(const QString& sourcePath, const QString& rootPath, QString& error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Não foi possível ler o plugin de reflexo selecionado: ") + sourcePath;
        return false;
    }
    const QByteArray bytes = source.readAll();
    const auto versionOf = [](const QByteArray& data) {
        const auto match = QRegularExpression(QStringLiteral("LudoReflectionSystem\\.js v([0-9]+\\.[0-9]+\\.[0-9]+)"))
            .match(QString::fromUtf8(data));
        return match.hasMatch() ? QVersionNumber::fromString(match.captured(1)) : QVersionNumber();
    };
    const auto sourceVersion = versionOf(bytes);
    if (bytes.isEmpty() || sourceVersion.isNull() || !bytes.contains("@target MZ")) {
        error = QStringLiteral("O arquivo selecionado não é um LudoReflectionSystem.js válido com versão identificável.");
        return false;
    }
    const QString target = QDir(rootPath).filePath(QStringLiteral("js/plugins/LudoReflectionSystem.js"));
    if (QFileInfo::exists(target)) {
        QFile existing(target);
        if (!existing.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("Não foi possível ler o plugin de reflexo instalado.");
            return false;
        }
        const QByteArray current = existing.readAll();
        existing.close();
        const auto currentVersion = versionOf(current);
        if (currentVersion.isNull() || QVersionNumber::compare(sourceVersion,currentVersion) < 0) {
            error = QStringLiteral("O plugin de reflexo instalado foi preservado: versão selecionada antiga ou versão instalada não identificável.");
            return false;
        }
        if (current == bytes) return true;
        if (!writeAtomic(target + QStringLiteral(".before-ludo-export.bak"),current,error)) return false;
    }
    if (!QDir(rootPath).mkpath(QStringLiteral("js/plugins"))) {
        error = QStringLiteral("Não foi possível criar js/plugins.");
        return false;
    }
    return writeAtomic(target,bytes,error);
}

inline bool isRpgMakerProjectRoot(const QString& path, core::RpgMakerEngine engine,
                                  QString* reason = nullptr)
{
    const QDir root(QDir::cleanPath(path));
    if (!root.exists()) {
        if (reason) *reason = QStringLiteral("A pasta não existe.");
        return false;
    }

    const QString extension = core::rpgMakerProjectExtension(engine);
    bool hasProjectFile = false;
    for (const QString& file : root.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (file.endsWith(extension, Qt::CaseInsensitive)) {
            hasProjectFile = true;
            break;
        }
    }
    if (!hasProjectFile) {
        if (reason)
            *reason = QStringLiteral("Não encontrei um arquivo %1 nessa pasta.").arg(extension);
        return false;
    }
    if (!QFileInfo::exists(root.filePath(QStringLiteral("data/MapInfos.json")))) {
        if (reason) *reason = QStringLiteral("Não encontrei data/MapInfos.json.");
        return false;
    }
    return true;
}

inline void eachTile(const core::LayerPtr& layer,
                     const std::function<void(const core::TileRef&, const QRectF&)>& fn)
{
    if (!layer) return;
    if (layer->type == core::LayerType::Tile) {
        for (int y = 0; y < layer->data2D.size(); ++y) {
            for (int x = 0; x < layer->data2D[y].size(); ++x) {
                for (const core::TileRef& tile : layer->data2D[y][x]) {
                    fn(tile, QRectF(x * layer->tileWidth + layer->offsetx,
                                    y * layer->tileHeight + layer->offsety,
                                    layer->tileWidth, layer->tileHeight));
                }
            }
        }
    } else if (layer->type == core::LayerType::Object) {
        for (const core::MapObject& object : layer->objects) {
            if (!object.visible) continue;
            const int sw = qMax(1, object.stampW);
            const int sh = qMax(1, object.stampH);
            const double cw = object.w / sw;
            const double ch = object.h / sh;
            for (int i = 0; i < object.tiles.size(); ++i) {
                fn(object.tiles[i], QRectF(object.x + layer->offsetx + (i % sw) * cw,
                                           object.y + layer->offsety + (i / sw) * ch,
                                           cw, ch));
            }
        }
    }
}

inline bool animatedTile(const core::Tileset& tileset, int tx, int ty,
                         const core::AnimatedAutotile** outAnimation = nullptr,
                         QPoint* outLocal = nullptr)
{
    QPoint local;
    const core::AnimatedAutotile* animation =
        core::animatedAutotileAt(tileset, tx, ty, true, nullptr, &local);
    const bool animated = animation && animation->valid() && animation->frameCount() > 1;
    if (outAnimation) *outAnimation = animated ? animation : nullptr;
    if (outLocal) *outLocal = local;
    return animated;
}

struct Check {
    QStringList errors;
    int animated = 0;
    int priority = 0;
    bool collision = false;
};

struct RuntimeOrdering {
    QHash<QString, int> layerBand;
    QHash<QString, QSet<quint64>> dynamicCells;
    int bandCount = 1;
};

inline quint64 runtimeCellKey(int x, int y)
{
    return (quint64(quint32(y)) << 32) | quint64(quint32(x));
}

inline bool runtimeDynamicTile(const core::Editor& editor, const core::TileRef& tile, bool forceAbove)
{
    const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
    if (!ts) return false;
    if (animatedTile(*ts, tile.tx, tile.ty)) return true;
    return !forceAbove && editor.tilePriority(tile.tilesetIdx, tile.tx, tile.ty) > 0;
}

inline RuntimeOrdering buildRuntimeOrdering(const core::Editor& editor, const core::MapDoc& doc)
{
    RuntimeOrdering out;
    int band = 0;
    int maxBand = 0;
    std::function<void(const QVector<core::LayerPtr>&, bool)> walk;
    walk = [&](const QVector<core::LayerPtr>& nodes, bool inheritedAbove) {
        for (const core::LayerPtr& layer : nodes) {
            if (!layer || !layer->visible || layer->opacity <= .001) continue;
            if (layer->parallaxLayer) continue;
            const bool forceAbove = inheritedAbove || layer->zMode == QStringLiteral("above");
            bool hasDynamic = false;

            if (layer->type != core::LayerType::Group) {
                out.layerBand.insert(layer->id, band);
                maxBand = qMax(maxBand, band);
            }

            if (layer->type == core::LayerType::Tile && (!layer->isMask || layer->maskShowBase)) {
                QSet<quint64> cells;
                for (int y = 0; y < layer->data2D.size(); ++y) {
                    const auto& row = layer->data2D[y];
                    for (int x = 0; x < row.size(); ++x) {
                        bool cellDynamic = false;
                        for (const core::TileRef& tile : row[x]) {
                            if (runtimeDynamicTile(editor, tile, forceAbove)) {
                                cellDynamic = true;
                                break;
                            }
                        }
                        if (cellDynamic) {
                            cells.insert(runtimeCellKey(x, y));
                            hasDynamic = true;
                        }
                    }
                }
                if (!cells.isEmpty()) out.dynamicCells.insert(layer->id, cells);
            } else if (layer->type == core::LayerType::Object && (!layer->isMask || layer->maskShowBase)) {
                for (const core::MapObject& object : layer->objects) {
                    if (!object.visible) continue;
                    for (const core::TileRef& tile : object.tiles) {
                        if (runtimeDynamicTile(editor, tile, forceAbove)) {
                            hasDynamic = true;
                            break;
                        }
                    }
                    if (hasDynamic) break;
                }
            }

            if (layer->isMask || layer->type == core::LayerType::Group)
                walk(layer->children, forceAbove);

            // Tudo que vier depois desta camada ganha uma banda acima dos
            // sprites dinâmicos dela. Assim os chunks podem ficar entre duas
            // camadas dinâmicas sem depender da ordem de addChild do PIXI.
            if (hasDynamic) ++band;
        }
    };
    walk(doc.layers, false);
    out.bandCount = qMax(1, maxBand + 1);
    return out;
}

inline Check check(const core::Editor& editor, const core::MapDoc& doc)
{
    Check out;
    const core::MapInfo& map = doc.map;
    const auto tree = core::validateLayerTree(doc.layers);
    if (!tree.ok) {
        out.errors << tree.error;
        return out;
    }

    if (map.width < 1 || map.height < 1 || map.width > 1000 || map.height > 1000 ||
        map.tileWidth < 1 || map.tileHeight < 1 || map.tileWidth > 256 || map.tileHeight > 256 ||
        qint64(map.width) * map.height > 250000 ||
        qint64(map.width) * map.height * map.tileWidth * map.tileHeight > 67108864) {
        out.errors << QStringLiteral("Divida o mapa: máximo 64 milhões de pixels, 250 mil células, 1000 células por eixo e tiles de até 256 px.");
    }

    if (map.depthEnabled) {
        QSet<QString> occupied;
        for (const auto& value : map.depthTransitions) {
            const auto t = value.toObject();
            const int x0=t.value("x0").toInt(-1), y0=t.value("y0").toInt(-1);
            const int x1=t.value("x1").toInt(-1), y1=t.value("y1").toInt(-1);
            if ((x0 == x1) == (y0 == y1) || qMin(x0,x1)<0 || qMin(y0,y1)<0 ||
                qMax(x0,x1)>=map.width || qMax(y0,y1)>=map.height) {
                out.errors << QStringLiteral("Profundidade: escada inválida. Revise início e fim nas configurações do mapa.");
                continue;
            }
            const int steps = qAbs(x1-x0)+qAbs(y1-y0);
            for(int i=0;i<=steps;++i) {
                const QString key=QString::number(x0+(x1-x0)*i/steps)+","+QString::number(y0+(y1-y0)*i/steps);
                if(occupied.contains(key)) out.errors << QStringLiteral("Profundidade: escadas sobrepostas.");
                occupied.insert(key);
            }
        }
    }

    int tileCount = 0;
    bool visualContent = false;
    bool dynamicInsideMask = false;
    std::function<void(const QVector<core::LayerPtr>&, bool, bool)> walk;
    walk = [&](const QVector<core::LayerPtr>& nodes, bool insideMask, bool inheritedAbove) {
        for (const core::LayerPtr& layer : nodes) {
            if (!layer || !layer->visible || layer->opacity <= .001) continue;
            if (layer->parallaxLayer) {
                if (layer->reflectionLayer || layer->isMask)
                    out.errors << QStringLiteral("Uma Camada Visual não pode ser Reflexo nem Clipping Mask: ") + layer->name;
                if (editor.rpgMakerEngine != core::RpgMakerEngine::MZ && layer->type != core::LayerType::Image)
                    out.errors << QStringLiteral("Camadas Visuais feitas de tiles, objetos, pintura agrupada ou pastas são exclusivas do RPG Maker MZ: ") + layer->name;
                if (editor.rpgMakerEngine != core::RpgMakerEngine::MZ &&
                    (layer->parallaxAnimationEnabled || layer->parallaxOscillationX > .001 ||
                     layer->parallaxOscillationY > .001 || layer->parallaxEffectPreset != QLatin1String("none")))
                    out.errors << QStringLiteral("Animação, balanço e efeitos de Camada Visual são exclusivos do RPG Maker MZ: ") + layer->name;
                if (layer->parallaxAnimationEnabled) {
                    if (layer->type != core::LayerType::Image)
                        out.errors << QStringLiteral("A animação por quadros precisa de uma Camada de Imagem: ") + layer->name;
                    else if (!layer->image.isNull()) {
                        const int columns = qBound(1, layer->parallaxAnimationColumns, 64);
                        const int rows = qBound(1, layer->parallaxAnimationRows, 64);
                        if (layer->image.width() % columns || layer->image.height() % rows)
                            out.errors << QStringLiteral("A imagem animada precisa ser divisível igualmente pelas colunas e linhas: ") + layer->name;
                        if (layer->parallaxAnimationFrames < 1 || layer->parallaxAnimationFrames > columns * rows)
                            out.errors << QStringLiteral("A quantidade de quadros ultrapassa a grade da imagem: ") + layer->name;
                    }
                }
                visualContent = true;
            }
            if (layer->type == core::LayerType::Image) {
                if (layer->reflectionLayer && editor.rpgMakerEngine != core::RpgMakerEngine::MZ)
                    out.errors << QStringLiteral("Camada de Reflexo é exclusiva do RPG Maker MZ: ") + layer->name;
                if (layer->imageReferenceOnly && !layer->reflectionLayer) continue;
                if (layer->image.isNull())
                    out.errors << QStringLiteral("Camada de imagem sem conteúdo visível: ") + layer->name;
                else if(!layer->reflectionLayer)
                    visualContent = true;
            }

            const bool forceAbove = inheritedAbove || layer->zMode == QStringLiteral("above");
            if (layer->isMask && layer->type != core::LayerType::Tile && layer->type != core::LayerType::Image)
                out.errors << QStringLiteral("Este tipo de camada não pode servir como Clipping Mask. Use uma camada de tiles, imagem ou pintura.");
            if (layer->type == core::LayerType::Tile &&
                (layer->tileWidth <= 0 || layer->tileHeight <= 0 || layer->cols < 0 || layer->rows < 0 ||
                 layer->data2D.size() != layer->rows)) {
                out.errors << QStringLiteral("Grade de camada inválida: ") + layer->name;
            }
            if (layer->type == core::LayerType::Tile) {
                for (const auto& row : layer->data2D) {
                    if (row.size() != layer->cols)
                        out.errors << QStringLiteral("Linha de camada inválida: ") + layer->name;
                }
            }
            if (layer->type == core::LayerType::Object) {
                for (const core::MapObject& object : layer->objects) {
                    if (!object.visible || std::abs(object.rotation) < .00001) continue;
                    // Objetos estáticos rotacionados são rasterizados normalmente
                    // nos chunks via Renderer/RotSprite. Conteúdo dinâmico ainda
                    // precisa de geometria por tile no runtime, então é bloqueado
                    // até o formato dinâmico carregar a transformação do objeto.
                    for (const core::TileRef& tile : object.tiles) {
                        const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
                        if (!ts) continue;
                        const bool dynamic = animatedTile(*ts, tile.tx, tile.ty) ||
                                             editor.tilePriority(tile.tilesetIdx, tile.tx, tile.ty) > 0 ||
                                             editor.collisionMask(tile.tilesetIdx, tile.tx, tile.ty) != 0;
                        if (dynamic) {
                            out.errors << QStringLiteral("Objeto rotacionado com animação, prioridade ou colisão ainda não pode ser exportado: ")
                                          + object.name;
                            break;
                        }
                    }
                }
            }

            if (!layer->isMask || layer->maskShowBase) {
                eachTile(layer, [&](const core::TileRef& tile, const QRectF& rect) {
                    ++tileCount;
                    const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
                    if (!ts || ts->image.isNull() || !ts->contains(tile.tx, tile.ty) ||
                        !ts->image.rect().contains(ts->tileRect(tile.tx, tile.ty))) {
                        out.errors << QStringLiteral("Tile sem imagem válida em ") + layer->name;
                        return;
                    }
                    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) ||
                        !std::isfinite(rect.width()) || !std::isfinite(rect.height()) ||
                        rect.width() <= 0 || rect.height() <= 0) {
                        out.errors << QStringLiteral("Objeto/tile com geometria inválida em ") + layer->name;
                    }

                    const bool isAnimated = animatedTile(*ts, tile.tx, tile.ty);
                    const int priority = editor.tilePriority(tile.tilesetIdx, tile.tx, tile.ty);
                    if (isAnimated) ++out.animated;
                    if (priority > 0) ++out.priority;
                    const bool rasterMasked = layer->imageMaskEnabled && !layer->imageMask.isNull();
                    if ((insideMask || rasterMasked) && (isAnimated || (priority > 0 && !forceAbove)))
                        dynamicInsideMask = true;

                    const int bits = editor.collisionMask(tile.tilesetIdx, tile.tx, tile.ty);
                    out.collision |= bits != 0;
                    if (bits != 0 && bits != 15) {
                        const auto aligned = [](double value, int size) {
                            return size > 0 && std::abs(value / size - std::round(value / size)) < .00001;
                        };
                        if (!aligned(rect.x(), map.tileWidth) || !aligned(rect.y(), map.tileHeight) ||
                            rect.width() != map.tileWidth || rect.height() != map.tileHeight) {
                            out.errors << QStringLiteral("Colisão direcional exige tile/objeto do tamanho de uma célula e alinhado à grade: ") + layer->name;
                        }
                    }
                });
            }

            if (layer->isMask) {
                walk(layer->children, true, forceAbove);
            } else if (layer->type == core::LayerType::Group) {
                walk(layer->children, insideMask, forceAbove);
            }
        }
    };
    walk(doc.layers, false, false);

    if (dynamicInsideMask) {
        out.errors << QStringLiteral("Esta combinação ainda não pode ser exportada: há tiles animados ou com prioridade dentro de uma camada usada como recorte ou com máscara. Coloque esses tiles em outra camada ou aplique o resultado antes de exportar.");
    }
    if (!tileCount && !visualContent)
        out.errors << QStringLiteral("Adicione pelo menos um tile, uma pintura ou uma imagem visível antes de exportar.");
    out.errors.removeDuplicates();
    return out;
}

// Renderiza somente a parte estática. Tiles animados e prioridades normais
// saem daqui e entram no array `dynamic`, porque precisam continuar vivos no runtime do RPG Maker.
inline bool hasRuntimeParallax(const QVector<core::LayerPtr>& nodes)
{
    for(const auto& layer:nodes){if(!layer||!layer->visible)continue;if(layer->parallaxLayer&&!layer->reflectionLayer)return true;if(layer->isContainer()&&hasRuntimeParallax(layer->children))return true;}return false;
}

inline QImage renderStatic(const core::Editor& editor, const core::MapDoc& doc,
                           const RuntimeOrdering& ordering, int targetBand,
                           const QRect& rect, bool above, int collisionBit = 0, int depthLevel = -1)
{
    QImage image(rect.size(), QImage::Format_ARGB32_Premultiplied);
    const bool baseBand = targetBand == 0;
    image.fill(baseBand && !above && !collisionBit && depthLevel <= 0 && !hasRuntimeParallax(doc.layers)
                   ? doc.map.background : QColor(Qt::transparent));

    QPainter painter(&image);
    painter.translate(-rect.x(), -rect.y());
    painter.setClipRect(rect);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (baseBand && !above && !collisionBit && depthLevel <= 0 && !hasRuntimeParallax(doc.layers)) core::drawMapPanorama(painter, doc.map);

    // O Runtime Contract v5 divide o cenário em bandas estáticas/dinâmicas.
    // Por isso não podemos chamar drawLayerTree() para a árvore inteira sem
    // filtrar a banda atual. Este walker mantém a mesma composição do editor,
    // incluindo a Sombra de Contato dos grupos, mas desenha somente as folhas
    // que pertencem ao passe atual.
    std::function<void(QPainter&, const QVector<core::LayerPtr>&, double, bool, bool)> walk;
    walk = [&](QPainter& target,
               const QVector<core::LayerPtr>& nodes,
               double alpha,
               bool inheritedAbove,
               bool allowContactShadows) {
        for (const core::LayerPtr& layer : nodes) {
            if (!layer || !layer->visible || alpha * layer->opacity <= .001) continue;
            // Uma Camada Visual é rasterizada uma única vez por
            // exportParallaxLayers(). Se for uma pasta, toda a subárvore sai
            // deste passe para não aparecer duplicada no cenário estático.
            if (layer->parallaxLayer && !layer->reflectionLayer) continue;
            const bool omitRuntimeImage=layer->type==core::LayerType::Image&&
                (layer->imageReferenceOnly||layer->parallaxLayer||layer->reflectionLayer);

            const bool forceAbove = inheritedAbove || layer->zMode == QStringLiteral("above");
            if (layer->type == core::LayerType::Image && forceAbove != above) continue;

            const bool targetLayer = ordering.layerBand.value(layer->id, 0) == targetBand;
            const bool targetDepth = depthLevel < 0 || layer->depthLevel == depthLevel;
            if (!omitRuntimeImage && targetLayer && targetDepth && layer->type != core::LayerType::Group &&
                (!layer->isMask || layer->maskShowBase)) {
                if (collisionBit) {
                    target.save();
                    target.setOpacity(1);
                    target.setPen(Qt::NoPen);
                    target.setBrush(Qt::white);
                    eachTile(layer, [&](const core::TileRef& tile, const QRectF& tileRect) {
                        const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
                        if (ts && (editor.collisionMask(tile.tilesetIdx, tile.tx, tile.ty) & collisionBit))
                            target.drawRect(tileRect);
                    });
                    target.restore();
                } else {
                    core::RenderOptions options;
                    options.fillBackground = false;
                    options.skipReferenceLayers = true;
                    options.drawObjectFrames = false;
                    options.animationTimeMs = 0;
                    // Ao construir a silhueta de uma sombra de Grupo, sombras
                    // internas não podem virar parte da própria silhueta.
                    options.suppressContactShadows = !allowContactShadows;
                    if (layer->type == core::LayerType::Tile) {
                        const QSet<quint64> runtimeCells = ordering.dynamicCells.value(layer->id);
                        if (!runtimeCells.isEmpty()) {
                            options.cellFilter = [runtimeCells](const core::LayerPtr&, int x, int y) {
                                return !runtimeCells.contains(runtimeCellKey(x, y));
                            };
                        }
                    }
                    options.tileFilter = [&](const core::TileRef& tile) {
                        const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
                        if (!ts) return false;
                        const bool isAnimated = animatedTile(*ts, tile.tx, tile.ty);
                        const int priority = editor.tilePriority(tile.tilesetIdx, tile.tx, tile.ty);
                        if (forceAbove) return above && !isAnimated;
                        if (isAnimated || priority > 0) return false;
                        return !above;
                    };
                    core::drawLayer(target, editor, layer, alpha, options);
                }
            }

            if (layer->isMask) {
                const QPainterPath mask=core::layerClipPath(layer,target.clipBoundingRect());
                target.save();
                target.setClipPath(mask, Qt::IntersectClip);
                walk(target, layer->children, alpha * layer->opacity, forceAbove, allowContactShadows);
                target.restore();
            } else if (layer->type == core::LayerType::Group) {
                // drawLayerTree() é quem normalmente cria a Sombra de Contato
                // do Grupo no editor. Como o exportador v5 usa um walker por
                // bandas, reproduzimos aqui exatamente a etapa de composição:
                // 1) rasteriza a silhueta estática dos filhos; 2) gera a sombra;
                // 3) desenha a sombra antes do conteúdo real do grupo.
                QVector<core::RasterLayerFilter> contactFilters;
                if (allowContactShadows && !collisionBit) {
                    for (const core::RasterLayerFilter& filter : layer->imageFilters) {
                        if (filter.enabled && filter.type == QLatin1String("contactShadow"))
                            contactFilters.push_back(filter);
                    }
                }

                if (!contactFilters.isEmpty() && !layer->children.isEmpty()) {
                    double maxReach = 4.0;
                    for (const core::RasterLayerFilter& filter : contactFilters)
                        maxReach = qMax(maxReach, filter.radius * 3.0 + filter.spread +
                                                  qAbs(filter.distance) + 4.0);
                    const int margin = qBound(4, int(std::ceil(maxReach)), 256);
                    const QRect mapBounds(0, 0, qMax(1, doc.map.pixelWidth()), qMax(1, doc.map.pixelHeight()));
                    QRectF viewport = target.clipBoundingRect();
                    if (!viewport.isValid() || viewport.isNull()) viewport = rect;
                    const QRect shadowRect = viewport.adjusted(-margin, -margin, margin, margin)
                                                     .intersected(QRectF(mapBounds).adjusted(-margin, -margin, margin, margin))
                                                     .toAlignedRect();

                    if (!shadowRect.isEmpty() && shadowRect.width() <= 12288 && shadowRect.height() <= 12288) {
                        QImage silhouette(shadowRect.size(), QImage::Format_ARGB32_Premultiplied);
                        silhouette.fill(Qt::transparent);
                        QPainter sp(&silhouette);
                        sp.setRenderHint(QPainter::SmoothPixmapTransform, false);
                        sp.translate(-shadowRect.x(), -shadowRect.y());
                        sp.setClipRect(shadowRect);
                        // Não inclui a opacidade do próprio grupo na silhueta;
                        // ela é aplicada ao desenhar a sombra, igual ao Renderer.
                        walk(sp, layer->children, 1.0, forceAbove, false);
                        sp.end();

                        QImage shadowOverlay(silhouette.size(), QImage::Format_ARGB32_Premultiplied);
                        shadowOverlay.fill(Qt::transparent);
                        QPainter op(&shadowOverlay);
                        for (const core::RasterLayerFilter& filter : contactFilters) {
                            const QImage oneShadow = core::contactShadowOverlayCached(silhouette, filter);
                            if (!oneShadow.isNull()) op.drawImage(QPoint(0, 0), oneShadow);
                        }
                        op.end();

                        if (!shadowOverlay.isNull()) {
                            target.save();
                            target.setCompositionMode(QPainter::CompositionMode_SourceOver);
                            target.setOpacity(qBound(0.0, alpha * layer->opacity, 1.0));
                            target.drawImage(shadowRect.topLeft(), shadowOverlay);
                            target.restore();
                        }
                    }
                }

                walk(target, layer->children, alpha * layer->opacity, forceAbove, allowContactShadows);
            }
        }
    };

    walk(painter, doc.layers, 1.0, false, true);
    painter.end();
    return image;
}

// Gera uma imagem chapada do cenário para servir como referência visual no
// editor do RPG Maker MV/MZ. Por padrão a referência é 1:1 nativa: nenhum pixel
// do mapa LUDO é redimensionado. Opcionalmente o usuário pode pedir ajuste ao
// grid nativo de 48 px por célula, preservando o comportamento histórico.
// A imagem NÃO é usada durante o Playtest: o LudoMapSystem a oculta em runtime.
inline bool renderReferenceParallax(const core::Editor& editor, const core::MapDoc& doc,
                                    bool fitToRpgMakerGrid, QImage& output, QString& error)
{
    constexpr int kRpgMakerTileSize = 48;
    constexpr qint64 kMaxReferencePixels = 67108864; // 64 MP; evita picos perigosos de RAM.
    constexpr int kMaxReferenceDimension = 16384;

    const qint64 targetWidth = fitToRpgMakerGrid
        ? qint64(doc.map.width) * kRpgMakerTileSize
        : qint64(doc.map.pixelWidth());
    const qint64 targetHeight = fitToRpgMakerGrid
        ? qint64(doc.map.height) * kRpgMakerTileSize
        : qint64(doc.map.pixelHeight());
    if (targetWidth <= 0 || targetHeight <= 0 ||
        targetWidth > kMaxReferenceDimension || targetHeight > kMaxReferenceDimension ||
        targetWidth * targetHeight > kMaxReferencePixels) {
        error = QStringLiteral("O mapa é grande demais para gerar com segurança o panorama de referência do RPG Maker. "
                               "Limite: 16384 px por eixo e 64 milhões de pixels.");
        return false;
    }

    output = QImage(QSize(int(targetWidth), int(targetHeight)), QImage::Format_ARGB32_Premultiplied);
    if (output.isNull()) {
        error = QStringLiteral("Memória insuficiente para gerar o panorama de referência do RPG Maker.");
        return false;
    }
    output.fill(doc.map.background);

    QPainter painter(&output);
    if (!painter.isActive()) {
        error = QStringLiteral("Não foi possível iniciar a renderização do panorama de referência.");
        output = QImage();
        return false;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (fitToRpgMakerGrid) {
        painter.scale(double(kRpgMakerTileSize) / qMax(1, doc.map.tileWidth),
                      double(kRpgMakerTileSize) / qMax(1, doc.map.tileHeight));
    }
    core::drawMapPanorama(painter,doc.map);
    core::RenderOptions options;
    options.fillBackground=false;
    options.skipReferenceLayers=true;
    options.drawObjectFrames=false;
    options.animationTimeMs=0;
    options.drawableFilter=[](const core::LayerPtr& layer){return !layer||!layer->reflectionLayer;};
    core::drawLayerTree(painter,editor,doc.layers,1.0,options);
    painter.end();
    return true;
}

inline QPainterPath collisionMaskPath(const core::LayerPtr& layer)
{
    QPainterPath mask;
    mask.setFillRule(Qt::WindingFill);
    if (!layer) return mask;
    for (int y = 0; y < layer->data2D.size(); ++y) {
        for (int x = 0; x < layer->data2D[y].size(); ++x) {
            if (!layer->data2D[y][x].isEmpty()) {
                mask.addRect(QRectF(x * layer->tileWidth + layer->offsetx,
                                    y * layer->tileHeight + layer->offsety,
                                    layer->tileWidth, layer->tileHeight));
            }
        }
    }
    return mask;
}

// Colisão é dado lógico de célula; não precisa ser rasterizada em quatro imagens
// por chunk. A versão 0.3.1 fazia isso e podia consumir CPU/memória desnecessária
// durante a exportação. Aqui calculamos diretamente as células afetadas, mantendo
// o mesmo comportamento de máscaras e de colisão direcional.
inline void collectCollision(const core::Editor& editor, const core::MapDoc& doc,
                             QVector<int>& collision, int depthLevel = -1)
{
    const core::MapInfo& map = doc.map;
    if (map.width <= 0 || map.height <= 0 || map.tileWidth <= 0 || map.tileHeight <= 0) return;

    auto applyTile = [&](const core::TileRef& tile, const QRectF& rawRect,
                         const QPainterPath& clip, bool hasClip) {
        const int bits = editor.collisionMask(tile.tilesetIdx, tile.tx, tile.ty);
        if (!bits) return;

        const QRectF rect = rawRect.normalized();
        if (!rect.isValid() || rect.isEmpty()) return;

        const int minX = qMax(0, int(std::floor(rect.left() / map.tileWidth)));
        const int minY = qMax(0, int(std::floor(rect.top() / map.tileHeight)));
        const int maxX = qMin(map.width - 1,
                              int(std::ceil(rect.right() / map.tileWidth)) - 1);
        const int maxY = qMin(map.height - 1,
                              int(std::ceil(rect.bottom() / map.tileHeight)) - 1);
        if (minX > maxX || minY > maxY) return;

        for (int cy = minY; cy <= maxY; ++cy) {
            for (int cx = minX; cx <= maxX; ++cx) {
                const QRectF cell(cx * map.tileWidth, cy * map.tileHeight,
                                  map.tileWidth, map.tileHeight);
                if (!rect.intersects(cell) && !rect.contains(cell.center())) continue;
                if (hasClip && !clip.intersects(cell) && !clip.contains(cell.center())) continue;
                collision[cy * map.width + cx] |= bits;
            }
        }
    };

    std::function<void(const QVector<core::LayerPtr>&, double, const QPainterPath&, bool)> walk;
    walk = [&](const QVector<core::LayerPtr>& nodes, double parentAlpha,
               const QPainterPath& parentClip, bool hasParentClip) {
        for (const core::LayerPtr& layer : nodes) {
            if (!layer || !layer->visible || parentAlpha * layer->opacity <= .001 ||
                layer->type == core::LayerType::Image) continue;
            if (layer->parallaxLayer) continue;

            if ((depthLevel < 0 || layer->depthLevel == depthLevel) && layer->type != core::LayerType::Group && (!layer->isMask || layer->maskShowBase)) {
                eachTile(layer, [&](const core::TileRef& tile, const QRectF& rect) {
                    applyTile(tile, rect, parentClip, hasParentClip);
                });
            }

            if (layer->isMask) {
                const QPainterPath ownMask = collisionMaskPath(layer);
                QPainterPath childClip = ownMask;
                bool hasChildClip = !ownMask.isEmpty();
                if (hasParentClip) {
                    childClip = hasChildClip ? parentClip.intersected(ownMask) : QPainterPath();
                    hasChildClip = hasChildClip && !childClip.isEmpty();
                }
                // Máscara vazia não deixa passar nada para os filhos.
                if (!ownMask.isEmpty() && hasChildClip)
                    walk(layer->children, parentAlpha * layer->opacity, childClip, true);
            } else if (layer->type == core::LayerType::Group) {
                walk(layer->children, parentAlpha * layer->opacity, parentClip, hasParentClip);
            }
        }
    };

    QPainterPath none;
    walk(doc.layers, 1.0, none, false);
}

inline QJsonObject exportDepth(const core::Editor& editor, const core::MapDoc& doc)
{
    QJsonArray levels;
    for (int level = 0; level < 2; ++level) {
        QVector<int> bits(doc.map.width * doc.map.height, 0);
        collectCollision(editor, doc, bits, level);
        QJsonArray cells;
        for (int b : bits) cells.append(b);
        levels.append(cells);
    }
    QVector<int> support(doc.map.width * doc.map.height, 0);
    std::function<void(const QVector<core::LayerPtr>&, bool, QPainterPath, bool)> walk;
    walk = [&](const QVector<core::LayerPtr>& layers, bool above, QPainterPath clip, bool clipped) {
        for (const auto& l : layers) {
            if (!l || !l->visible || l->opacity <= .001) continue;
            if (l->parallaxLayer) continue;
            const bool overhead = above || l->zMode == QStringLiteral("above");
            if (l->depthLevel == 1 && !overhead && (!l->isMask || l->maskShowBase)) {
                eachTile(l, [&](const core::TileRef&, const QRectF& rect) {
                    for (int y = qMax(0, int(std::floor(rect.top()/doc.map.tileHeight)));
                         y < qMin(doc.map.height, int(std::ceil(rect.bottom()/doc.map.tileHeight))); ++y)
                        for (int x = qMax(0, int(std::floor(rect.left()/doc.map.tileWidth)));
                             x < qMin(doc.map.width, int(std::ceil(rect.right()/doc.map.tileWidth))); ++x) {
                            const QRectF cell(x*doc.map.tileWidth,y*doc.map.tileHeight,doc.map.tileWidth,doc.map.tileHeight);
                            if (!clipped || clip.intersects(cell) || clip.contains(cell.center()))
                                support[y * doc.map.width + x] = 1;
                        }
                });
            }
            if (l->isMask) {
                const QPainterPath own = collisionMaskPath(l);
                const QPainterPath next = clipped ? clip.intersected(own) : own;
                if (!next.isEmpty()) walk(l->children, overhead, next, true);
            } else walk(l->children, overhead, clip, clipped);
        }
    };
    walk(doc.layers, false, QPainterPath(), false);
    QJsonArray coverage;
    for (int cell : support) coverage.append(cell);
    return QJsonObject{{"enabled", true}, {"scale", 1.0},
        {"startLevel", doc.map.depthStartLevel}, {"transitions", doc.map.depthTransitions},
        {"collision", levels}, {"support", coverage}};
}

inline bool emptyImage(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x])) return false;
        }
    }
    return true;
}

inline QJsonArray frameRects(const core::Tileset& ts, const core::TileRef& tile,
                             const core::AnimatedAutotile* animation, const QPoint& local)
{
    QJsonArray frames;
    auto appendRect = [&](const QRect& rect) {
        QJsonArray frame;
        frame.append(rect.x());
        frame.append(rect.y());
        frame.append(rect.width());
        frame.append(rect.height());
        frames.append(frame);
    };

    if (animation && animation->frameCount() > 1) {
        for (const QPoint& origin : animation->frameOrigins) {
            const int tx = origin.x() + local.x();
            const int ty = origin.y() + local.y();
            appendRect(ts.contains(tx, ty) ? ts.tileRect(tx, ty) : ts.tileRect(tile.tx, tile.ty));
        }
    } else {
        appendRect(ts.tileRect(tile.tx, tile.ty));
    }
    return frames;
}

inline void appendDynamicTile(const core::Editor& editor, int mapId,
                              const core::TileRef& tile, const QRectF& rect,
                              bool forceAbove, double opacity, const QString& blendMode,
                              quint32 instanceSeed, bool forceRuntime, int renderOrder, int stackOrder,
                              QJsonArray& dynamic, QSet<int>& usedTilesets)
{
    const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
    if (!ts || ts->image.isNull() || !ts->contains(tile.tx, tile.ty)) return;

    const core::AnimatedAutotile* animation = nullptr;
    QPoint local;
    const bool isAnimated = animatedTile(*ts, tile.tx, tile.ty, &animation, &local);
    const int priority = editor.tilePriority(tile.tilesetIdx, tile.tx, tile.ty);

    // Uma camada explicitamente Above já vence a ordenação Y; tile estático
    // nessa camada pode continuar no chunk. Animação continua dinâmica.
    if (!forceRuntime && !isAnimated && (forceAbove || priority <= 0)) return;

    QJsonObject item;
    item.insert(QStringLiteral("atlas"), atlasName(mapId, tile.tilesetIdx));
    item.insert(QStringLiteral("x"), rect.x());
    item.insert(QStringLiteral("y"), rect.y());
    item.insert(QStringLiteral("width"), rect.width());
    item.insert(QStringLiteral("height"), rect.height());
    item.insert(QStringLiteral("opacity"), qBound(0.0, opacity, 1.0));
    item.insert(QStringLiteral("blendMode"), blendMode);
    item.insert(QStringLiteral("priority"), forceAbove ? 0 : priority);
    item.insert(QStringLiteral("mode"), forceAbove
                    ? QStringLiteral("above")
                    : (priority > 0 ? QStringLiteral("priority") : QStringLiteral("below")));
    item.insert(QStringLiteral("renderOrder"), qMax(0, renderOrder));
    item.insert(QStringLiteral("stackOrder"), qMax(0, stackOrder));
    item.insert(QStringLiteral("frames"), frameRects(*ts, tile, animation, local));

    if (animation && isAnimated) {
        item.insert(QStringLiteral("fps"), qBound(0.1, animation->fps, 120.0));
        item.insert(QStringLiteral("loop"), animation->loop);
        item.insert(QStringLiteral("pingPong"), animation->pingPong);
        item.insert(QStringLiteral("synchronized"), animation->synchronized);
        const int sequence = animation->pingPong
            ? qMax(1, animation->frameCount() * 2 - 2)
            : animation->frameCount();
        item.insert(QStringLiteral("phase"), animation->synchronized || sequence <= 1
                    ? 0 : int(instanceSeed % quint32(sequence)));
    } else {
        item.insert(QStringLiteral("fps"), 0.0);
        item.insert(QStringLiteral("loop"), false);
        item.insert(QStringLiteral("pingPong"), false);
        item.insert(QStringLiteral("synchronized"), true);
        item.insert(QStringLiteral("phase"), 0);
    }

    dynamic.append(item);
    usedTilesets.insert(tile.tilesetIdx);
}

inline void collectDynamic(const core::Editor& editor, const core::MapDoc& doc, int mapId,
                           const RuntimeOrdering& ordering, QJsonArray& dynamic, QSet<int>& usedTilesets)
{
    std::function<void(const QVector<core::LayerPtr>&, double, bool)> walk;
    walk = [&](const QVector<core::LayerPtr>& nodes, double parentAlpha, bool inheritedAbove) {
        for (const core::LayerPtr& layer : nodes) {
            if (!layer || !layer->visible || parentAlpha * layer->opacity <= .001 ||
                layer->type == core::LayerType::Image) continue;
            if (layer->parallaxLayer) continue;
            const bool forceAbove = inheritedAbove || layer->zMode == QStringLiteral("above");
            const double opacity = parentAlpha * layer->opacity;
            const int dynamicStart = dynamic.size();

            if (layer->type != core::LayerType::Group && (!layer->isMask || layer->maskShowBase)) {
                if (layer->type == core::LayerType::Tile) {
                    const QSet<quint64> runtimeCells = ordering.dynamicCells.value(layer->id);
                    const int renderOrder = ordering.layerBand.value(layer->id, 0);
                    for (int y = 0; y < layer->data2D.size(); ++y) {
                        for (int x = 0; x < layer->data2D[y].size(); ++x) {
                            const bool wholeCellRuntime = runtimeCells.contains(runtimeCellKey(x, y));
                            const auto& cell = layer->data2D[y][x];
                            for (int stackOrder = 0; stackOrder < cell.size(); ++stackOrder) {
                                const core::TileRef& tile = cell[stackOrder];
                                const QRectF rect(x * layer->tileWidth + layer->offsetx,
                                                  y * layer->tileHeight + layer->offsety,
                                                  layer->tileWidth, layer->tileHeight);
                                const quint32 seed = quint32((x * 73856093) ^ (y * 19349663) ^ (stackOrder * 83492791));
                                appendDynamicTile(editor, mapId, tile, rect, forceAbove, opacity,
                                                  layer->blendMode, seed, wholeCellRuntime, renderOrder, stackOrder,
                                                  dynamic, usedTilesets);
                            }
                        }
                    }
                } else if (layer->type == core::LayerType::Object) {
                    for (const core::MapObject& object : layer->objects) {
                        if (!object.visible) continue;
                        const int sw = qMax(1, object.stampW);
                        const int sh = qMax(1, object.stampH);
                        const double cw = object.w / sw;
                        const double ch = object.h / sh;
                        for (int i = 0; i < object.tiles.size(); ++i) {
                            const QRectF rect(object.x + layer->offsetx + (i % sw) * cw,
                                              object.y + layer->offsety + (i / sw) * ch,
                                              cw, ch);
                            const quint32 seed = quint32(qHash(object.id) ^ quint32(i));
                            appendDynamicTile(editor, mapId, object.tiles[i], rect, forceAbove,
                                              opacity, layer->blendMode, seed, false,
                                              ordering.layerBand.value(layer->id, 0), i, dynamic, usedTilesets);
                        }
                    }
                }
            }

            for (int i = dynamicStart; i < dynamic.size(); ++i) {
                auto item = dynamic.at(i).toObject();
                item.insert("level", layer->depthLevel);
                dynamic.replace(i, item);
            }
            if (layer->isMask || layer->type == core::LayerType::Group)
                walk(layer->children, opacity, forceAbove);
        }
    };
    walk(doc.layers, 1.0, false);
}

inline bool objectReflectionEnabled(const core::MapObject& object)
{
    return object.properties.value(QStringLiteral("ludoReflection")).compare(
               QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

inline void collectReflectionObjects(const core::Editor& editor, const core::MapDoc& doc, int mapId,
                                     QJsonArray& reflectedObjects, QSet<int>& usedTilesets)
{
    std::function<void(const QVector<core::LayerPtr>&, double)> walk;
    walk = [&](const QVector<core::LayerPtr>& nodes, double parentAlpha) {
        for (const core::LayerPtr& layer : nodes) {
            if (!layer || !layer->visible || parentAlpha * layer->opacity <= .001) continue;
            if (layer->parallaxLayer) continue;
            const double opacity = parentAlpha * layer->opacity;
            if (layer->type == core::LayerType::Object) {
                for (const core::MapObject& object : layer->objects) {
                    if (!object.visible || !objectReflectionEnabled(object) || object.tiles.isEmpty()) continue;
                    QJsonObject item{
                        {QStringLiteral("id"), object.id},
                        {QStringLiteral("name"), object.name},
                        {QStringLiteral("x"), object.x + layer->offsetx},
                        {QStringLiteral("y"), object.y + layer->offsety},
                        {QStringLiteral("width"), object.w},
                        {QStringLiteral("height"), object.h},
                        {QStringLiteral("rotation"), object.rotation},
                        {QStringLiteral("opacity"), qBound(0.0, opacity, 1.0)},
                        {QStringLiteral("level"), layer->depthLevel}
                    };
                    bool reflectionOffsetOk = false;
                    const int reflectionOffset = object.properties.value(QStringLiteral("ludoReflectionOffsetY")).toInt(&reflectionOffsetOk);
                    if (reflectionOffsetOk && reflectionOffset != 0)
                        item.insert(QStringLiteral("reflectionOffsetY"), qBound(-96, reflectionOffset, 96));
                    QJsonArray parts;
                    const int sw = qMax(1, object.stampW);
                    const int sh = qMax(1, object.stampH);
                    const double cw = object.w / sw;
                    const double ch = object.h / sh;
                    for (int i = 0; i < object.tiles.size(); ++i) {
                        const core::TileRef& tile = object.tiles[i];
                        const core::Tileset* ts = editor.tilesetAt(tile.tilesetIdx);
                        if (!ts || ts->image.isNull() || !ts->contains(tile.tx, tile.ty)) continue;
                        const core::AnimatedAutotile* animation = nullptr;
                        QPoint local;
                        const bool isAnimated = animatedTile(*ts, tile.tx, tile.ty, &animation, &local);
                        QJsonObject part{
                            {QStringLiteral("atlas"), atlasName(mapId, tile.tilesetIdx)},
                            {QStringLiteral("x"), (i % sw) * cw},
                            {QStringLiteral("y"), (i / sw) * ch},
                            {QStringLiteral("width"), cw},
                            {QStringLiteral("height"), ch},
                            {QStringLiteral("frames"), frameRects(*ts, tile, animation, local)}
                        };
                        if (animation && isAnimated) {
                            part.insert(QStringLiteral("fps"), qBound(0.1, animation->fps, 120.0));
                            part.insert(QStringLiteral("loop"), animation->loop);
                            part.insert(QStringLiteral("pingPong"), animation->pingPong);
                            part.insert(QStringLiteral("synchronized"), animation->synchronized);
                            const int sequence = animation->pingPong
                                ? qMax(1, animation->frameCount() * 2 - 2)
                                : animation->frameCount();
                            part.insert(QStringLiteral("phase"), animation->synchronized || sequence <= 1
                                ? 0 : int((qHash(object.id) ^ quint32(i)) % quint32(sequence)));
                        }
                        parts.append(part);
                        usedTilesets.insert(tile.tilesetIdx);
                    }
                    if (!parts.isEmpty()) {
                        item.insert(QStringLiteral("parts"), parts);
                        reflectedObjects.append(item);
                    }
                }
            }
            if (layer->isMask || layer->type == core::LayerType::Group)
                walk(layer->children, opacity);
        }
    };
    walk(doc.layers, 1.0);
}

struct ReflectionResourceBinding {
    QString preset;
    int opacityPercent = -1; // -1 = intensidade original do preset
    QString blurMode = QStringLiteral("none");
    int blurStrength = 0;

    QString surfaceKey() const {
        return QStringLiteral("%1|%2|%3").arg(preset, blurMode).arg(blurStrength);
    }
};

inline QHash<QString, ReflectionResourceBinding> reflectionResourceIndex(const core::Editor& editor)
{
    QHash<QString, ReflectionResourceBinding> out;
    for (int tsIdx=0; tsIdx<editor.tilesets.size(); ++tsIdx) {
        const core::Tileset& ts=editor.tilesets.at(tsIdx);
        for (auto it=ts.tileResourceEffects.constBegin(); it!=ts.tileResourceEffects.constEnd(); ++it) {
            const QString preset=core::resourceEffectPreset(it.value(),QStringLiteral("reflection"));
            if (preset.isEmpty()) continue;
            const QStringList xy=it.key().split(QLatin1Char(':')); if(xy.size()!=2) continue;
            ReflectionResourceBinding binding;
            binding.preset = preset;
            binding.opacityPercent = core::resourceEffectOpacityPercent(it.value(), QStringLiteral("reflection"));
            binding.blurMode = core::resourceEffectBlurMode(it.value(), QStringLiteral("reflection"));
            binding.blurStrength = core::resourceEffectBlurStrength(it.value(), QStringLiteral("reflection"));
            out.insert(core::tileKey(tsIdx,xy[0].toInt(),xy[1].toInt()), binding);
        }
    }
    for (const core::TilesetAutotile& autotile : editor.autotiles) {
        const QString preset=core::resourceEffectPreset(autotile.resourceEffects,QStringLiteral("reflection"));
        if (preset.isEmpty()) continue;
        int owner=-1; for(int i=0;i<editor.tilesets.size();++i) if(editor.tilesets.at(i).id==autotile.tilesetId){owner=i;break;}
        if(owner<0) continue;
        ReflectionResourceBinding binding;
        binding.preset = preset;
        binding.opacityPercent = core::resourceEffectOpacityPercent(autotile.resourceEffects, QStringLiteral("reflection"));
        binding.blurMode = core::resourceEffectBlurMode(autotile.resourceEffects, QStringLiteral("reflection"));
        binding.blurStrength = core::resourceEffectBlurStrength(autotile.resourceEffects, QStringLiteral("reflection"));
        for(const QPoint& pt:core::tilesetAutotileTiles(editor,owner,autotile))
            out.insert(core::tileKey(owner,pt.x(),pt.y()),binding);
    }
    return out;
}

inline bool exportTilesetReflectionMasks(const core::Editor& editor, const core::MapDoc& doc, int mapId,
                                         const QDir& stageDir, QJsonArray& surfaces,
                                         QSet<QString>& generatedImages, QString& error)
{
    const QHash<QString,ReflectionResourceBinding> bindings=reflectionResourceIndex(editor);
    if(bindings.isEmpty()) return true;
    struct Draw { core::TileRef tile; QRectF dst; int mapX=0,mapY=0; ReflectionResourceBinding binding; };
    QHash<quint64,Draw> chosen;
    std::function<void(const QVector<core::LayerPtr>&,double)> walk;
    walk=[&](const QVector<core::LayerPtr>& nodes,double parentOpacity){
        for(const core::LayerPtr& layer:nodes){
            if(!layer||!layer->visible||parentOpacity*layer->opacity<=.001) continue;
            const double opacity=parentOpacity*layer->opacity;
            if(layer->type==core::LayerType::Tile){
                for(int y=0;y<layer->rows;++y) for(int x=0;x<layer->cols;++x){
                    const core::Cell cell=layer->cellAt(x,y);
                    for(const core::TileRef& tile:cell){
                        const core::Tileset* tileTs = editor.tilesetAt(tile.tilesetIdx);
                        if (!tileTs) continue;
                        const QPoint canonical = core::canonicalAnimatedTile(*tileTs, tile.tx, tile.ty);
                        const ReflectionResourceBinding binding=bindings.value(core::tileKey(tile.tilesetIdx,canonical.x(),canonical.y()));
                        if(binding.preset.isEmpty()) continue;
                        const QRectF dst(layer->offsetx+x*layer->tileWidth,layer->offsety+y*layer->tileHeight,layer->tileWidth,layer->tileHeight);
                        const int mx=qBound(0,int(std::floor((dst.center().x())/qMax(1,doc.map.tileWidth))),qMax(0,doc.map.width-1));
                        const int my=qBound(0,int(std::floor((dst.center().y())/qMax(1,doc.map.tileHeight))),qMax(0,doc.map.height-1));
                        const quint64 key=(quint64(quint32(my))<<32)|quint64(quint32(mx));
                        chosen.insert(key,Draw{tile,dst,mx,my,binding});
                    }
                }
            }
            if(layer->isMask||layer->type==core::LayerType::Group) walk(layer->children,opacity);
        }
    };
    walk(doc.layers,1.0);
    if(chosen.isEmpty()) return true;

    struct Piece { QImage image; int x=0,y=0; };
    QHash<QString,QHash<QString,Piece>> piecesBySurface;
    QHash<QString,QSet<quint64>> cellsBySurface;
    QHash<QString,ReflectionResourceBinding> bindingBySurface;
    for(auto it=chosen.constBegin();it!=chosen.constEnd();++it){
        const Draw& draw=it.value(); const core::Tileset* ts=editor.tilesetAt(draw.tile.tilesetIdx);
        if(!ts||ts->image.isNull()||!ts->contains(draw.tile.tx,draw.tile.ty)) continue;
        const QString surfaceKey = draw.binding.surfaceKey();
        cellsBySurface[surfaceKey].insert(it.key());
        bindingBySurface.insert(surfaceKey, draw.binding);
        const QRect bounds=draw.dst.toAlignedRect();
        const int minCx=(qMax(0,bounds.left())/512)*512, maxCx=(qMax(0,bounds.right())/512)*512;
        const int minCy=(qMax(0,bounds.top())/512)*512, maxCy=(qMax(0,bounds.bottom())/512)*512;
        for(int cy=minCy;cy<=maxCy;cy+=512) for(int cx=minCx;cx<=maxCx;cx+=512){
            const QRect chunk(cx,cy,qMin(512,doc.map.pixelWidth()-cx),qMin(512,doc.map.pixelHeight()-cy)); if(chunk.isEmpty()) continue;
            if(!chunk.intersects(bounds)) continue;
            const QString ck=QStringLiteral("%1,%2").arg(cx).arg(cy); Piece& piece=piecesBySurface[surfaceKey][ck];
            if(piece.image.isNull()){piece.x=cx;piece.y=cy;piece.image=QImage(chunk.size(),QImage::Format_ARGB32_Premultiplied);piece.image.fill(Qt::transparent);}
            QPainter painter(&piece.image); painter.setRenderHint(QPainter::SmoothPixmapTransform,false); painter.translate(-cx,-cy);
            if (draw.binding.opacityPercent >= 0) painter.setOpacity(qBound(0, draw.binding.opacityPercent, 100) / 100.0);
            painter.drawImage(draw.dst,ts->image,ts->tileRect(draw.tile.tx,draw.tile.ty)); painter.end();
        }
    }
    QStringList surfaceKeys=cellsBySurface.keys(); std::sort(surfaceKeys.begin(),surfaceKeys.end());
    for(const QString& surfaceKey:surfaceKeys){
        const ReflectionResourceBinding binding = bindingBySurface.value(surfaceKey);
        const QString preset = binding.preset;
        QJsonObject surface;
        surface.insert(QStringLiteral("preset"),preset);
        if (binding.blurMode != QLatin1String("none") && binding.blurStrength > 0) {
            surface.insert(QStringLiteral("blurMode"), binding.blurMode);
            surface.insert(QStringLiteral("blurStrength"), qBound(0, binding.blurStrength, 24));
        }
        QJsonArray cells;
        QVector<quint64> ordered; for(quint64 key:cellsBySurface[surfaceKey]) ordered.push_back(key); std::sort(ordered.begin(),ordered.end());
        for(quint64 key:ordered) cells.append(QStringLiteral("%1,%2").arg(int(quint32(key&0xffffffffu))).arg(int(quint32(key>>32))));
        surface.insert(QStringLiteral("cells"),cells); QJsonArray masks;
        auto pieces=piecesBySurface.value(surfaceKey); QStringList keys=pieces.keys(); std::sort(keys.begin(),keys.end()); int ordinal=0;
        QString fileTag = surfaceKey; fileTag.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        for(const QString& key:keys){ const Piece& piece=pieces[key]; if(piece.image.isNull()||emptyImage(piece.image)) continue;
            const QString name=QStringLiteral("%1_reflect_%2_%3.png").arg(mapPrefix(mapId),fileTag).arg(ordinal++);
            if(!piece.image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/")+name),"PNG")){error=QStringLiteral("Falha ao gravar máscara de reflexo: ")+name;return false;}
            generatedImages.insert(name); masks.append(QJsonObject{{"file",name},{"x",piece.x},{"y",piece.y},{"width",piece.image.width()},{"height",piece.image.height()}});
        }
        surface.insert(QStringLiteral("masks"),masks); surfaces.append(surface);
    }
    return true;
}

inline bool exportAuthoredReflectionLayers(const core::Editor& editor,const core::MapDoc& doc,int mapId,
                                            const QDir& stageDir,QJsonArray& surfaces,
                                            QSet<QString>& generatedImages,QString& error)
{
    int ordinal=0;
    std::function<void(const QVector<core::LayerPtr>&)> walk;
    walk=[&](const QVector<core::LayerPtr>& nodes){
        for(const auto& layer:nodes){
            if(!layer||!layer->visible)continue;
            if(layer->type==core::LayerType::Image&&layer->reflectionLayer&&!layer->image.isNull()){
                QJsonArray masks,cells;int pieceNo=0;
                QSet<QString> occupiedCells;
                for(int cy=0;cy<doc.map.pixelHeight()&&error.isEmpty();cy+=512)for(int cx=0;cx<doc.map.pixelWidth()&&error.isEmpty();cx+=512){
                    const QRect chunk(cx,cy,qMin(512,doc.map.pixelWidth()-cx),qMin(512,doc.map.pixelHeight()-cy));
                    QImage image(chunk.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
                    QPainter painter(&image);painter.translate(-chunk.x(),-chunk.y());
                    core::RenderOptions options;options.fillBackground=false;options.drawObjectFrames=false;
                    core::drawLayer(painter,editor,layer,1.0,options);painter.end();
                    if(emptyImage(image))continue;
                    const QString name=QStringLiteral("%1_reflect_layer_%2_%3.png").arg(mapPrefix(mapId)).arg(ordinal).arg(pieceNo++);
                    if(!image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/")+name),"PNG")){error=QStringLiteral("Falha ao gravar Camada de Reflexo: ")+name;break;}
                    generatedImages.insert(name);masks.append(QJsonObject{{"file",name},{"x",cx},{"y",cy},{"width",image.width()},{"height",image.height()}});
                    const QImage alpha=image.convertToFormat(QImage::Format_ARGB32);
                    const int tw=qMax(1,doc.map.tileWidth),th=qMax(1,doc.map.tileHeight);
                    for(int my=cy/th;my<=qMin(doc.map.height-1,(cy+image.height()-1)/th);++my)for(int mx=cx/tw;mx<=qMin(doc.map.width-1,(cx+image.width()-1)/tw);++mx){
                        const QRect localCell(mx*tw-cx,my*th-cy,tw,th);const QRect sample=localCell.intersected(alpha.rect());bool occupied=false;
                        for(int py=sample.top();py<=sample.bottom()&&!occupied;++py){const QRgb* row=reinterpret_cast<const QRgb*>(alpha.constScanLine(py));for(int px=sample.left();px<=sample.right();++px)if(qAlpha(row[px])>0){occupied=true;break;}}
                        if(occupied)occupiedCells.insert(QStringLiteral("%1,%2").arg(mx).arg(my));
                    }
                }
                if(!masks.isEmpty()){
                    QStringList orderedCells=occupiedCells.values();std::sort(orderedCells.begin(),orderedCells.end());for(const QString& cell:orderedCells)cells.append(cell);
                    const QString preset=layer->reflectionPreset==QLatin1String("water")?QStringLiteral("still"):layer->reflectionPreset;
                    QJsonObject surface{{"preset",preset},{"opacity",qRound(layer->reflectionOpacity*2.55)},
                        {"waveX",layer->reflectionWave/4.0},{"waveY",layer->reflectionWave/32.0},
                        {"blurMode",layer->reflectionBlur>0?QStringLiteral("both"):QStringLiteral("none")},
                        {"blurStrength",layer->reflectionBlur},{"cells",cells},{"masks",masks}};
                    surfaces.append(surface);
                }
                ++ordinal;
            }
            if(layer->isContainer())walk(layer->children);
        }
    };walk(doc.layers);return error.isEmpty();
}

inline bool exportParallaxLayers(const core::Editor& editor,const core::MapDoc& doc,int mapId,
                                 const QDir& stageDir,QJsonArray& output,
                                 QSet<QString>& generatedImages,QString& error)
{
    int ordinal=0;const QRect mapRect(0,0,doc.map.pixelWidth(),doc.map.pixelHeight());
    if(hasRuntimeParallax(doc.layers)&&doc.map.panoramaVisible&&!doc.map.panorama.isNull()){
        QImage image(mapRect.size(),QImage::Format_ARGB32_Premultiplied);image.fill(doc.map.background);
        QPainter painter(&image);core::drawMapPanorama(painter,doc.map);painter.end();
        const QString name=QStringLiteral("%1_parallax_map.png").arg(mapPrefix(mapId));
        if(!image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/")+name),"PNG")){error=QStringLiteral("Falha ao gravar o panorama do mapa: ")+name;return false;}
        generatedImages.insert(name);output.append(QJsonObject{{"file",name},{"x",0},{"y",0},{"factorX",1.0},{"factorY",1.0},{"speedX",0},{"speedY",0},{"repeatX",false},{"repeatY",false},{"oscillationX",0},{"oscillationY",0},{"oscillationSpeed",0},{"effect",QStringLiteral("none")},{"opacity",1.0},{"blendMode",QStringLiteral("source-over")},{"plane",QStringLiteral("below")},{"parallaxOrder",0}});++ordinal;
    }

    auto alphaBounds=[](const QImage& image){
        int left=image.width(),top=image.height(),right=-1,bottom=-1;
        const QImage rgba=image.convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<rgba.height();++y){const QRgb* row=reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
            for(int x=0;x<rgba.width();++x)if(qAlpha(row[x])){left=qMin(left,x);right=qMax(right,x);top=qMin(top,y);bottom=qMax(bottom,y);}}
        return right<left||bottom<top?QRect():QRect(QPoint(left,top),QPoint(right,bottom));
    };
    auto sourceType=[](core::LayerType type){
        switch(type){case core::LayerType::Tile:return QStringLiteral("tiles");case core::LayerType::Object:return QStringLiteral("objects");case core::LayerType::Group:return QStringLiteral("group");case core::LayerType::Image:return QStringLiteral("image");}
        return QStringLiteral("layer");
    };

    std::function<void(const QVector<core::LayerPtr>&,double,bool)> walk;
    walk=[&](const QVector<core::LayerPtr>& nodes,double parentOpacity,bool inheritedAbove){
        for(const auto& layer:nodes){
            if(!layer||!layer->visible)continue;
            const bool above=inheritedAbove||layer->zMode==QLatin1String("above");
            if(layer->parallaxLayer&&!layer->reflectionLayer){
                if(editor.rpgMakerEngine!=core::RpgMakerEngine::MZ&&layer->type!=core::LayerType::Image)continue;
                if(layer->type==core::LayerType::Image&&layer->image.isNull())continue;

                core::LayerPtr renderLayer=core::cloneLayer(layer,false);
                // A repetição pertence ao runtime. A textura exportada contém
                // somente uma cópia, evitando costuras e arquivos do tamanho do mapa.
                if(renderLayer->type==core::LayerType::Image){renderLayer->imageRepeatX=false;renderLayer->imageRepeatY=false;}
                const bool animated=editor.rpgMakerEngine==core::RpgMakerEngine::MZ&&
                    renderLayer->type==core::LayerType::Image&&renderLayer->parallaxAnimationEnabled;
                const int frameCount=animated?qBound(1,renderLayer->parallaxAnimationFrames,
                    qMax(1,renderLayer->parallaxAnimationColumns*renderLayer->parallaxAnimationRows)):1;
                QRect bounds=renderLayer->type==core::LayerType::Image
                    ? core::imageLayerTransform(renderLayer).mapRect(QRectF(QPointF(0,0),QSizeF(core::visualLayerFrameSize(renderLayer)))).toAlignedRect()
                    : mapRect;
                if(bounds.isEmpty())continue;

                auto renderFrame=[&](int frame){
                    QImage image(bounds.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
                    QPainter painter(&image);painter.translate(-bounds.topLeft());painter.setClipRect(bounds);
                    core::RenderOptions options;options.fillBackground=false;options.drawObjectFrames=false;
                    options.previewVisualEffects=false;
                    if(animated)options.animationTimeMs=qint64(std::ceil(frame*1000.0/qMax(.1,renderLayer->parallaxAnimationFps)+.01));
                    if(renderLayer->type==core::LayerType::Group)
                        core::drawLayerTree(painter,editor,QVector<core::LayerPtr>{renderLayer},parentOpacity,options);
                    else core::drawLayer(painter,editor,renderLayer,parentOpacity,options);
                    painter.end();return image;
                };

                QImage first=renderFrame(0);
                if(renderLayer->type!=core::LayerType::Image){
                    const QRect occupied=alphaBounds(first);if(occupied.isEmpty())continue;
                    bounds=occupied.translated(bounds.topLeft());first=first.copy(occupied);
                }else if(emptyImage(first))continue;

                const int sheetColumns=animated?qMax(1,int(std::ceil(std::sqrt(double(frameCount))))):1;
                const int sheetRows=animated?int(std::ceil(double(frameCount)/sheetColumns)):1;
                const qint64 sheetPixels=qint64(first.width())*sheetColumns*first.height()*sheetRows;
                if(first.width()*sheetColumns>16384||first.height()*sheetRows>16384||sheetPixels>67108864){
                    error=QStringLiteral("A animação da Camada Visual ‘%1’ ficou grande demais. Reduza os quadros ou o tamanho da imagem.").arg(layer->name);return;
                }
                QImage sheet(QSize(first.width()*sheetColumns,first.height()*sheetRows),QImage::Format_ARGB32_Premultiplied);sheet.fill(Qt::transparent);
                QPainter sheetPainter(&sheet);sheetPainter.drawImage(QPoint(0,0),first);
                for(int frame=1;frame<frameCount;++frame)
                    sheetPainter.drawImage(QPoint((frame%sheetColumns)*first.width(),(frame/sheetColumns)*first.height()),renderFrame(frame));
                sheetPainter.end();

                const int layerOrder=ordinal++;
                const QString name=QStringLiteral("%1_parallax_%2.png").arg(mapPrefix(mapId)).arg(layerOrder);
                if(!sheet.save(stageDir.filePath(QStringLiteral("img/ludoMaps/")+name),"PNG")){error=QStringLiteral("Falha ao gravar Camada Visual: ")+name;return;}
                generatedImages.insert(name);
                const bool repeatX=layer->parallaxRepeatX||(layer->type==core::LayerType::Image&&layer->imageRepeatX);
                const bool repeatY=layer->parallaxRepeatY||(layer->type==core::LayerType::Image&&layer->imageRepeatY);
                QJsonObject item{{"file",name},{"name",layer->name},{"sourceType",sourceType(layer->type)},
                    {"x",bounds.x()},{"y",bounds.y()},{"factorX",layer->parallaxFactorX},{"factorY",layer->parallaxFactorY},
                    {"speedX",layer->parallaxSpeedX},{"speedY",layer->parallaxSpeedY},{"repeatX",repeatX},{"repeatY",repeatY},
                    {"oscillationX",layer->parallaxOscillationX},{"oscillationY",layer->parallaxOscillationY},{"oscillationSpeed",layer->parallaxOscillationSpeed},
                    {"smoothMotion",layer->parallaxSmoothMotion},
                    {"effect",layer->parallaxEffectPreset},{"effectStrength",layer->parallaxEffectStrength},{"effectSpeed",layer->parallaxEffectSpeed},
                    {"opacity",1.0},{"blendMode",layer->blendMode},{"plane",above?QStringLiteral("above"):QStringLiteral("below")},{"parallaxOrder",layerOrder}};
                if(animated)item.insert(QStringLiteral("animation"),QJsonObject{{"frameWidth",first.width()},{"frameHeight",first.height()},
                    {"frameCount",frameCount},{"sheetColumns",sheetColumns},{"fps",layer->parallaxAnimationFps},{"pingPong",layer->parallaxAnimationPingPong}});
                output.append(item);
                continue;
            }
            if(layer->isContainer())walk(layer->children,parentOpacity*layer->opacity,above);
        }
    };walk(doc.layers,1.0,false);return error.isEmpty();
}

inline bool exportReflectionEnvironment(const core::MapDoc& doc, int mapId, const QDir& stageDir,
                                        QJsonObject& runtimeEnvironment, QSet<QString>& generatedImages,
                                        QString& error)
{
    runtimeEnvironment = QJsonObject();
    const QJsonObject settings = doc.reflectionSettings.value(QStringLiteral("environment")).toObject();
    if (!settings.value(QStringLiteral("enabled")).toBool(false)) return true;

    const QString rawSourcePath = settings.value(QStringLiteral("sourcePath")).toString().trimmed();
    const QString sourcePath = rawSourcePath.isEmpty() ? QString() : QDir::cleanPath(rawSourcePath);
    if (sourcePath.isEmpty()) {
        error = QStringLiteral("O reflexo de céu/ambiente está ativado, mas nenhuma imagem foi selecionada.");
        return false;
    }
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        error = QStringLiteral("A imagem de céu/ambiente não foi encontrada: ") + sourcePath;
        return false;
    }

    const QImage image(sourcePath);
    if (image.isNull()) {
        error = QStringLiteral("Não foi possível ler a imagem de céu/ambiente: ") + sourcePath;
        return false;
    }

    const QString name = QStringLiteral("%1_reflect_environment.png").arg(mapPrefix(mapId));
    const QString stagedPath = stageDir.filePath(QStringLiteral("img/ludoMaps/") + name);
    if (!image.save(stagedPath, "PNG")) {
        error = QStringLiteral("Falha ao exportar a imagem de céu/ambiente: ") + name;
        return false;
    }
    generatedImages.insert(name);

    runtimeEnvironment = settings;
    runtimeEnvironment.remove(QStringLiteral("sourcePath"));
    runtimeEnvironment.insert(QStringLiteral("enabled"), true);
    runtimeEnvironment.insert(QStringLiteral("file"), name);
    runtimeEnvironment.insert(QStringLiteral("opacity"),
                              qBound(0, settings.value(QStringLiteral("opacity")).toInt(112), 255));
    const QString fit = settings.value(QStringLiteral("fit")).toString(QStringLiteral("cover"));
    runtimeEnvironment.insert(QStringLiteral("fit"),
                              fit == QLatin1String("stretch") ? QStringLiteral("stretch") : QStringLiteral("cover"));
    runtimeEnvironment.insert(QStringLiteral("flipY"), settings.value(QStringLiteral("flipY")).toBool(true));
    const QString blend = settings.value(QStringLiteral("blendMode")).toString(QStringLiteral("normal"));
    runtimeEnvironment.insert(QStringLiteral("blendMode"),
                              (blend == QLatin1String("screen") || blend == QLatin1String("add"))
                                  ? blend : QStringLiteral("normal"));
    return true;
}

inline QJsonArray mapVariationManifest(const core::Editor& editor, const core::MapDoc& doc)
{
    const QString baseId = doc.variationBaseId.isEmpty() ? doc.id : doc.variationBaseId;
    const core::MapDoc* base = editor.mapById(baseId);
    if (!base) return QJsonArray();

    QJsonArray out;
    auto append = [&](const core::MapDoc& map, const QString& label, bool isBase) {
        if (map.rpgMakerMapId <= 0) return;
        QJsonObject item;
        item.insert(QStringLiteral("name"), label);
        item.insert(QStringLiteral("mapId"), map.rpgMakerMapId);
        if (isBase) item.insert(QStringLiteral("base"), true);
        out.append(item);
    };
    append(*base, QStringLiteral("Base"), true);
    for (const core::MapDoc& candidate : editor.docs) {
        if (candidate.variationBaseId != baseId) continue;
        append(candidate,
               candidate.variationName.trimmed().isEmpty() ? candidate.name : candidate.variationName.trimmed(),
               false);
    }
    return out.size() > 1 ? out : QJsonArray();
}

inline QJsonObject reflectionManifest(const core::MapDoc& doc, const QJsonArray& objects,
                                      const QJsonArray& tileSurfaces = QJsonArray(),
                                      const QJsonObject& runtimeEnvironment = QJsonObject())
{
    QJsonObject reflection = doc.reflectionSettings;
    // sourcePath pertence apenas ao projeto de autoria. O runtime recebe somente
    // o arquivo copiado para img/ludoMaps, evitando caminhos absolutos do PC.
    reflection.remove(QStringLiteral("environment"));
    if (!runtimeEnvironment.isEmpty()) reflection.insert(QStringLiteral("environment"), runtimeEnvironment);
    if (!objects.isEmpty()) reflection.insert(QStringLiteral("objects"), objects);
    if (!tileSurfaces.isEmpty()) {
        reflection.insert(QStringLiteral("source"),QStringLiteral("tileset"));
        reflection.insert(QStringLiteral("tileSurfaces"),tileSurfaces);
        reflection.insert(QStringLiteral("enabled"),true);
    }
    return reflection;
}

inline QJsonObject makeDefaultRpgMakerMap(const core::MapDoc& doc)
{
    const QJsonObject silentAudio{
        {QStringLiteral("name"), QString()},
        {QStringLiteral("pan"), 0},
        {QStringLiteral("pitch"), 100},
        {QStringLiteral("volume"), 90}
    };
    QJsonArray events;
    events.append(QJsonValue(QJsonValue::Null));

    return QJsonObject{
        {QStringLiteral("autoplayBgm"), false},
        {QStringLiteral("autoplayBgs"), false},
        {QStringLiteral("battleback1Name"), QString()},
        {QStringLiteral("battleback2Name"), QString()},
        {QStringLiteral("bgm"), silentAudio},
        {QStringLiteral("bgs"), silentAudio},
        {QStringLiteral("disableDashing"), false},
        {QStringLiteral("displayName"), QString()},
        {QStringLiteral("encounterList"), QJsonArray()},
        {QStringLiteral("encounterStep"), 30},
        {QStringLiteral("height"), doc.map.height},
        {QStringLiteral("note"), QString()},
        {QStringLiteral("parallaxLoopX"), false},
        {QStringLiteral("parallaxLoopY"), false},
        {QStringLiteral("parallaxName"), QString()},
        {QStringLiteral("parallaxShow"), true},
        {QStringLiteral("parallaxSx"), 0},
        {QStringLiteral("parallaxSy"), 0},
        {QStringLiteral("scrollType"), 0},
        {QStringLiteral("specifyBattleback"), false},
        {QStringLiteral("tilesetId"), 1},
        {QStringLiteral("width"), doc.map.width},
        {QStringLiteral("data"), QJsonArray()},
        {QStringLiteral("events"), events}
    };
}

inline QString prepareLudoMapNote(QString note, const QString& referenceName)
{
    // Atualiza nossa tag sem acumular versões antigas a cada exportação.
    static const QRegularExpression referenceTag(
        QStringLiteral(R"(<\s*LudoReferenceParallax\s*:[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    note.remove(referenceTag);
    note = note.trimmed();
    if (!note.contains(QStringLiteral("<LudoMap>"), Qt::CaseInsensitive)) {
        if (!note.isEmpty()) note += QLatin1Char('\n');
        note += QStringLiteral("<LudoMap>");
    }
    if (!note.isEmpty()) note += QLatin1Char('\n');
    note += QStringLiteral("<LudoReferenceParallax:%1>").arg(referenceName);
    return note;
}

inline bool makePreparedMapJson(const QString& sourcePath, const core::MapDoc& doc,
                                const QString& referenceName, QByteArray& output,
                                bool& created, QString& error)
{
    QJsonObject map;
    created = !QFileInfo::exists(sourcePath);
    if (!created) {
        QFile file(sourcePath);
        if (!file.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("Não foi possível ler ") + sourcePath;
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            error = QStringLiteral("O MapXXX.json do RPG Maker está inválido: ") + parseError.errorString();
            return false;
        }
        map = document.object();
    } else {
        map = makeDefaultRpgMakerMap(doc);
    }

    // Guardamos a camada de Regiões existente antes de redimensionar/zerar o
    // MapXXX. Projetos antigos do LUDO ainda não possuem autoria de regiões;
    // nesse caso preservamos as regiões feitas diretamente no RPG Maker até o usuário
    // editar a primeira região no Desktop.
    const int previousWidth = qMax(0, map.value(QStringLiteral("width")).toInt());
    const int previousHeight = qMax(0, map.value(QStringLiteral("height")).toInt());
    const QJsonArray previousData = map.value(QStringLiteral("data")).toArray();

    map.insert(QStringLiteral("note"), prepareLudoMapNote(map.value(QStringLiteral("note")).toString(), referenceName));
    map.insert(QStringLiteral("width"), doc.map.width);
    map.insert(QStringLiteral("height"), doc.map.height);
    map.insert(QStringLiteral("scrollType"), 0); // integração não suporta loop

    // Panorama somente de REFERÊNCIA DO EDITOR. O plugin reconhece a tag acima
    // e o oculta durante o Playtest, evitando duplicação com o cenário LUDO.
    map.insert(QStringLiteral("parallaxName"), referenceName);
    map.insert(QStringLiteral("parallaxShow"), true);
    map.insert(QStringLiteral("parallaxLoopX"), false);
    map.insert(QStringLiteral("parallaxLoopY"), false);
    map.insert(QStringLiteral("parallaxSx"), 0);
    map.insert(QStringLiteral("parallaxSy"), 0);

    if (!map.value(QStringLiteral("events")).isArray()) {
        QJsonArray events;
        events.append(QJsonValue(QJsonValue::Null));
        map.insert(QStringLiteral("events"), events);
    }

    QJsonArray data;
    const qint64 layerCells = qint64(doc.map.width) * doc.map.height;
    const qint64 cells = layerCells * 6;
    for (qint64 i = 0; i < cells; ++i) data.append(0);

    // SAFE SYNC: se o MapXXX já existe, o LUDO preserva TODAS as camadas
    // nativas do RPG Maker MV/MZ (z=0..5) dentro da área comum. Isso evita que
    // vincular/salvar um mapa criado no RPG Maker apague seus tiles nativos. Quando
    // o tamanho muda, apenas a área que ainda existe é copiada.
    if (!created && previousWidth > 0 && previousHeight > 0 && !previousData.isEmpty()) {
        const qint64 previousCells = qint64(previousWidth) * previousHeight;
        if (previousData.size() >= previousCells * 6) {
            const int copyWidth = qMin(previousWidth, doc.map.width);
            const int copyHeight = qMin(previousHeight, doc.map.height);
            for (int z = 0; z < 6; ++z) {
                for (int y = 0; y < copyHeight; ++y) {
                    for (int x = 0; x < copyWidth; ++x) {
                        const qint64 oldIndex = (qint64(z) * previousHeight + y) * previousWidth + x;
                        const qint64 newIndex = (qint64(z) * doc.map.height + y) * doc.map.width + x;
                        if (oldIndex >= 0 && oldIndex < previousData.size() &&
                            newIndex >= 0 && newIndex < data.size()) {
                            data[int(newIndex)] = previousData.at(int(oldIndex));
                        }
                    }
                }
            }
        }
    }

    // RPG Maker MV/MZ usa z=5 para Region ID. Só substituímos essa camada quando
    // o usuário realmente criou/editou Regiões no LUDO. Caso contrário, a
    // região nativa preservada acima continua intacta.
    if (doc.rpgMakerRegionsAuthored) {
        for (int y = 0; y < doc.map.height; ++y) {
            for (int x = 0; x < doc.map.width; ++x) {
                const qint64 index = (qint64(5) * doc.map.height + y) * doc.map.width + x;
                if (index >= 0 && index < data.size()) data[int(index)] = 0;
            }
        }
        for (auto it = doc.rpgMakerRegions.cbegin(); it != doc.rpgMakerRegions.cend(); ++it) {
            const int x = core::MapDoc::regionX(it.key());
            const int y = core::MapDoc::regionY(it.key());
            const int regionId = qBound(0, int(it.value()), 255);
            if (regionId <= 0 || !doc.regionInBounds(x, y)) continue;
            const qint64 index = (qint64(5) * doc.map.height + y) * doc.map.width + x;
            if (index >= 0 && index < data.size()) data[int(index)] = regionId;
        }
    }
    map.insert(QStringLiteral("data"), data);

    output = QJsonDocument(map).toJson(QJsonDocument::Compact);
    return true;
}

inline bool makePreparedMapInfosJson(const QString& sourcePath, int mapId,
                                     const core::MapDoc& doc, QByteArray& output,
                                     bool& infoCreated, QString& error)
{
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Não foi possível ler ") + sourcePath;
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        error = QStringLiteral("data/MapInfos.json está inválido: ") + parseError.errorString();
        return false;
    }

    QJsonArray infos = document.array();
    int maxOrder = 0;
    for (const QJsonValue& value : infos) {
        if (value.isObject()) maxOrder = qMax(maxOrder, value.toObject().value(QStringLiteral("order")).toInt());
    }
    while (infos.size() <= mapId) infos.append(QJsonValue(QJsonValue::Null));

    infoCreated = !infos.at(mapId).isObject();
    QJsonObject info = infoCreated ? QJsonObject() : infos.at(mapId).toObject();
    if (infoCreated) {
        QString name = doc.name.trimmed();
        if (name.isEmpty()) name = QStringLiteral("LUDO Map %1").arg(mapId);
        info.insert(QStringLiteral("expanded"), false);
        info.insert(QStringLiteral("name"), name);
        info.insert(QStringLiteral("order"), maxOrder + 1);
        info.insert(QStringLiteral("parentId"), 0);
        info.insert(QStringLiteral("scrollX"), 0);
        info.insert(QStringLiteral("scrollY"), 0);
    } else if (info.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        info.insert(QStringLiteral("name"), doc.name.trimmed().isEmpty()
                    ? QStringLiteral("LUDO Map %1").arg(mapId) : doc.name.trimmed());
    }
    info.insert(QStringLiteral("id"), mapId);
    infos[mapId] = info;

    output = QJsonDocument(infos).toJson(QJsonDocument::Compact);
    return true;
}

inline void applyEditorMapInfoHierarchy(const core::Editor& editor, const core::MapDoc& doc,
                                        int targetMapId, QByteArray& preparedMapInfos,
                                        int explicitParentId = -1)
{
    QJsonParseError parse;
    const QJsonDocument parsed = QJsonDocument::fromJson(preparedMapInfos, &parse);
    if (parse.error != QJsonParseError::NoError || !parsed.isArray()) return;
    QJsonArray infos = parsed.array();
    if (targetMapId <= 0 || targetMapId >= infos.size() || !infos.at(targetMapId).isObject()) return;

    QJsonObject info = infos.at(targetMapId).toObject();
    info.insert(QStringLiteral("name"), doc.name.trimmed().isEmpty()
                    ? QStringLiteral("LUDO Map %1").arg(targetMapId)
                    : doc.name.trimmed());

    int parentMapId = 0;
    if (explicitParentId >= 0) {
        parentMapId = explicitParentId;
    } else if (!doc.parentId.isEmpty()) {
        if (const core::MapDoc* parent = editor.mapById(doc.parentId))
            parentMapId = qMax(0, parent->rpgMakerMapId);
    }
    info.insert(QStringLiteral("parentId"), parentMapId);

    // MapInfos.order é global no MV/MZ. Usar a mesma sequência do Editor faz
    // a árvore persistir igual nos dois lados, inclusive depois de exportar um
    // mapa isoladamente (o export antigo podia recriar o item como raiz).
    for (int i = 0; i < editor.docs.size(); ++i) {
        if (editor.docs[i].id == doc.id) {
            info.insert(QStringLiteral("order"), i + 1);
            break;
        }
    }
    infos[targetMapId] = info;
    preparedMapInfos = QJsonDocument(infos).toJson(QJsonDocument::Compact);
}

inline bool saveFirstBackup(const QString& source, const QString& backup, QString& error)
{
    if (QFileInfo::exists(backup)) return true;
    return copyAtomic(source, backup, error);
}

inline void cleanupStaleMapImages(const QString& folder, const QString& prefix,
                                  const QSet<QString>& keep)
{
    QDir dir(folder);
    if (!dir.exists()) return;
    const QStringList files = dir.entryList(QStringList{prefix + QStringLiteral("_*.png")}, QDir::Files);
    for (const QString& file : files) {
        if (!keep.contains(file)) QFile::remove(dir.filePath(file));
    }
}

inline bool exportBoundMap(const core::Editor& editor, const core::MapDoc& sourceDoc,
                           const QString& rootPathInput, int targetMapId,
                           bool fitReferenceGrid, QWidget* parent,
                           bool showSuccess = false, int publicationParentId = -1)
{
    const QString engineName = core::rpgMakerEngineName(editor.rpgMakerEngine);
    const QString engineShort = core::rpgMakerEngineId(editor.rpgMakerEngine).toUpper();
    const bool supportsExtraReflection = editor.rpgMakerEngine == core::RpgMakerEngine::MZ;
    const Check validation = check(editor, sourceDoc);
    if (!validation.errors.isEmpty()) {
        if (parent)
            QMessageBox::warning(parent, QStringLiteral("Revise o mapa antes de salvar"),
                                 validation.errors.join(QLatin1Char('\n')));
        return false;
    }
    QString rootReason;
    if (!isRpgMakerProjectRoot(rootPathInput, editor.rpgMakerEngine, &rootReason) || targetMapId <= 0) {
        if (parent)
            QMessageBox::warning(parent, QStringLiteral("Sincronização %1").arg(core::rpgMakerEngineName(editor.rpgMakerEngine)),
                                 targetMapId <= 0
                                     ? QStringLiteral("Este mapa ainda não possui um ID do %1.").arg(core::rpgMakerEngineName(editor.rpgMakerEngine))
                                     : rootReason);
        return false;
    }

    const QString rootPath = QDir::cleanPath(rootPathInput);

    try {
    const QDir rpgMakerRoot(rootPath);
    const QString prefix = mapPrefix(targetMapId);
    const QString targetMapPath = rpgMakerRoot.filePath(QStringLiteral("data/") + mapJsonName(targetMapId));

    // Log próprio, síncrono e com flush. O log Qt do launcher pode ficar em 0 KB
    // quando o Windows encerra o processo por corrupção de heap antes do flush.
    // Este arquivo permite identificar a última fase concluída mesmo nesse caso.
    const QString traceDir = rpgMakerRoot.filePath(QStringLiteral("data/ludoMaps"));
    QDir().mkpath(traceDir);
    QFile traceFile(QDir(traceDir).filePath(QStringLiteral("LUDO_EXPORT_TRACE.txt")));
    traceFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    auto trace = [&](const QString& line) {
        if (!traceFile.isOpen()) return;
        traceFile.write((line + QLatin1Char('\n')).toUtf8());
        traceFile.flush();
    };
    trace(QStringLiteral("BEGIN editor=%1 engine=%2 map=%3")
              .arg(QString::fromLatin1(core::version::Editor))
              .arg(engineShort)
              .arg(targetMapId));
    trace(QStringLiteral("referenceMode=%1").arg(fitReferenceGrid ? QStringLiteral("rpgmaker-grid-48") : QStringLiteral("native-1to1")));

    trace(QStringLiteral("phase=prepare-json"));
    const QString mapInfosPath = rpgMakerRoot.filePath(QStringLiteral("data/MapInfos.json"));
    const QString referenceBase = referenceParallaxBaseName(targetMapId);
    const QString referenceFile = referenceParallaxFileName(targetMapId);
    QByteArray preparedMap;
    QByteArray preparedMapInfos;
    bool mapCreated = false;
    bool mapInfoCreated = false;
    QString error;
    if (!makePreparedMapJson(targetMapPath, sourceDoc, referenceBase, preparedMap, mapCreated, error) ||
        !makePreparedMapInfosJson(mapInfosPath, targetMapId, sourceDoc, preparedMapInfos, mapInfoCreated, error)) {
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"), error);
        return false;
    }

    applyEditorMapInfoHierarchy(editor, sourceDoc, targetMapId, preparedMapInfos, publicationParentId);

    trace(QStringLiteral("phase=snapshot"));
    const core::MapDoc doc = sourceDoc;
    const core::MapInfo& map = doc.map;
    trace(QStringLiteral("regions=%1 authored=%2")
              .arg(doc.rpgMakerRegions.size())
              .arg(doc.rpgMakerRegionsAuthored ? QStringLiteral("yes") : QStringLiteral("preserve-rpgmaker")));
    const RuntimeOrdering runtimeOrdering = buildRuntimeOrdering(editor, doc);
    QJsonArray dynamic;
    QJsonArray reflectionObjects;
    QSet<int> dynamicTilesets;
    collectDynamic(editor, doc, targetMapId, runtimeOrdering, dynamic, dynamicTilesets);
    if (supportsExtraReflection)
        collectReflectionObjects(editor, doc, targetMapId, reflectionObjects, dynamicTilesets);
    trace(QStringLiteral("phase=dynamic done count=%1 reflectionObjects=%2 atlases=%3")
              .arg(dynamic.size()).arg(reflectionObjects.size()).arg(dynamicTilesets.size()));

    QTemporaryDir stage(rpgMakerRoot.filePath(QStringLiteral(".ludo-") + core::rpgMakerEngineId(editor.rpgMakerEngine) + QStringLiteral("-export-XXXXXX")));
    if (!stage.isValid()) {
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"),
                             QStringLiteral("Não foi possível criar a área temporária dentro do projeto %1. Confira as permissões.").arg(engineName));
        return false;
    }
    QDir stageDir(stage.path());
    if (!stageDir.mkpath(QStringLiteral("data/ludoMaps")) ||
        !stageDir.mkpath(QStringLiteral("img/ludoMaps")) ||
        !stageDir.mkpath(QStringLiteral("img/parallaxes"))) {
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"),
                             QStringLiteral("Não foi possível preparar as pastas temporárias da exportação."));
        return false;
    }

    trace(QStringLiteral("phase=stage-ready"));
    QJsonArray chunks;
    QVector<int> collision(map.width * map.height, 0);
    collectCollision(editor, doc, collision);
    trace(QStringLiteral("phase=collision done cells=%1").arg(collision.size()));

    const int chunkCount = ((map.pixelWidth() + 511) / 512) * ((map.pixelHeight() + 511) / 512);
    const int progressMax = qMax(1, chunkCount + dynamicTilesets.size() + 1);
    // Top-level e sem QObject parent: evita qualquer relação de ownership com o
    // MainWindow enquanto a exportação pesada está em andamento.
    QProgressDialog progress(QStringLiteral("Exportando mapa para %1…").arg(engineName),
                             QString(), 0, progressMax, nullptr);
    progress.setWindowTitle(QStringLiteral("LUDO → %1").arg(engineName));
    // Não bombeamos o event loop inteiro durante a exportação. Na 0.3.1,
    // QApplication::processEvents() permitia reentrância no MainWindow enquanto
    // o mapa estava sendo renderizado/gravado.
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.setAutoReset(false);
    progress.setAutoClose(false);
    progress.show();
    progress.repaint();

    int done = 0;
    QSet<QString> generatedImages;

    trace(QStringLiteral("phase=reference begin file=%1").arg(referenceFile));
    QImage referenceImage;
    if (!renderReferenceParallax(editor, doc, fitReferenceGrid, referenceImage, error) ||
        !referenceImage.save(stageDir.filePath(QStringLiteral("img/parallaxes/") + referenceFile), "PNG")) {
        if (error.isEmpty()) error = QStringLiteral("Falha ao gravar o panorama de referência ") + referenceFile;
    }
    referenceImage = QImage(); // libera a maior alocação antes de renderizar os chunks.
    if (error.isEmpty()) {
        trace(QStringLiteral("phase=reference done"));
        progress.setValue(++done);
        progress.repaint();
    }

    for (int y = 0; y < map.pixelHeight() && error.isEmpty(); y += 512) {
        for (int x = 0; x < map.pixelWidth() && error.isEmpty(); x += 512) {
            const QRect rect(x, y, qMin(512, map.pixelWidth() - x), qMin(512, map.pixelHeight() - y));
            trace(QStringLiteral("phase=chunk x=%1 y=%2 w=%3 h=%4").arg(x).arg(y).arg(rect.width()).arg(rect.height()));

            for (int renderOrder = 0; renderOrder < runtimeOrdering.bandCount && error.isEmpty(); ++renderOrder) {
                for (int level = 0; level < (map.depthEnabled ? 2 : 1); ++level) for (bool above : {false, true}) {
                    const QImage image = renderStatic(editor, doc, runtimeOrdering, renderOrder, rect, above, 0, map.depthEnabled ? level : -1);
                    if (image.isNull()) {
                        error = QStringLiteral("Memória insuficiente para gerar o cenário.");
                        break;
                    }
                    if ((above || level > 0 || renderOrder > 0) && emptyImage(image)) continue;
                    const QString plane = above ? QStringLiteral("above") : QStringLiteral("below");
                    const QString name = QStringLiteral("%1_%2_%3_%4_L%5_R%6.png")
                                             .arg(prefix).arg(x).arg(y).arg(plane).arg(level).arg(renderOrder);
                    if (!image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/") + name), "PNG")) {
                        error = QStringLiteral("Falha ao gravar a imagem ") + name;
                        break;
                    }
                    generatedImages.insert(name);
                    chunks.append(QJsonObject{
                        {QStringLiteral("file"), name},
                        {QStringLiteral("plane"), plane},
                        {QStringLiteral("level"), level},
                        {QStringLiteral("renderOrder"), renderOrder},
                        {QStringLiteral("x"), x},
                        {QStringLiteral("y"), y},
                        {QStringLiteral("width"), rect.width()},
                        {QStringLiteral("height"), rect.height()}
                    });
                }
            }
            progress.setValue(++done);
            progress.repaint();
        }
    }

    trace(QStringLiteral("phase=chunks done count=%1").arg(chunks.size()));
    for (int tilesetIdx : dynamicTilesets) {
        if (!error.isEmpty()) break;
        const core::Tileset* ts = editor.tilesetAt(tilesetIdx);
        if (!ts || ts->image.isNull()) {
            error = QStringLiteral("Tileset dinâmico inválido durante a exportação.");
            break;
        }
        const QString name = atlasName(targetMapId, tilesetIdx);
        if (!ts->image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/") + name), "PNG")) {
            error = QStringLiteral("Falha ao exportar o atlas dinâmico ") + name;
            break;
        }
        generatedImages.insert(name);
        progress.setValue(++done);
        progress.repaint();
    }

    trace(QStringLiteral("phase=atlases done"));
    QJsonArray parallaxLayers;
    if(error.isEmpty()&&!exportParallaxLayers(editor,doc,targetMapId,stageDir,parallaxLayers,generatedImages,error))
        trace(QStringLiteral("ERROR parallax-layers: ")+error);
    QJsonArray cells;
    for (int bits : collision) cells.append(bits);
    QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("ludo-map")},
        {QStringLiteral("version"), 5},
        {QStringLiteral("exporter"), QStringLiteral("LUDO Map Editor Runtime Contract v5")},
        {QStringLiteral("mapId"), targetMapId},
        {QStringLiteral("width"), map.width},
        {QStringLiteral("height"), map.height},
        {QStringLiteral("tileWidth"), map.tileWidth},
        {QStringLiteral("tileHeight"), map.tileHeight},
        {QStringLiteral("collision"), cells},
        {QStringLiteral("regionCount"), doc.rpgMakerRegions.size()},
        {QStringLiteral("regionsAuthored"), doc.rpgMakerRegionsAuthored},
        {QStringLiteral("chunks"), chunks},
        {QStringLiteral("dynamic"), dynamic},
        {QStringLiteral("renderOrderCount"), runtimeOrdering.bandCount},
        {QStringLiteral("textureFiltering"), QStringLiteral("nearest")}
    };
    if(!parallaxLayers.isEmpty()){manifest.insert(QStringLiteral("parallaxLayers"),parallaxLayers);manifest.insert(QStringLiteral("backgroundColor"),map.background.name(QColor::HexRgb));}
    const QJsonArray mapVariations = mapVariationManifest(editor, doc);
    if (!mapVariations.isEmpty()) {
        const QString variationBaseId = doc.variationBaseId.isEmpty() ? doc.id : doc.variationBaseId;
        const core::MapDoc* variationBase = editor.mapById(variationBaseId);
        manifest.insert(QStringLiteral("variations"), mapVariations);
        if (variationBase && variationBase->rpgMakerMapId > 0)
            manifest.insert(QStringLiteral("variationBaseMapId"), variationBase->rpgMakerMapId);
        if (!doc.variationBaseId.isEmpty())
            manifest.insert(QStringLiteral("variationName"), doc.variationName);
    }
    if (supportsExtraReflection) {
        QJsonArray tilesetReflectionSurfaces;
        if (error.isEmpty() && !exportTilesetReflectionMasks(editor, doc, targetMapId, stageDir, tilesetReflectionSurfaces, generatedImages, error))
            trace(QStringLiteral("ERROR reflection-masks: ") + error);
        if(error.isEmpty()&&!exportAuthoredReflectionLayers(editor,doc,targetMapId,stageDir,tilesetReflectionSurfaces,generatedImages,error))
            trace(QStringLiteral("ERROR reflection-layers: ")+error);
        QJsonObject reflectionEnvironment;
        if (error.isEmpty() && !exportReflectionEnvironment(doc, targetMapId, stageDir, reflectionEnvironment, generatedImages, error))
            trace(QStringLiteral("ERROR reflection-environment: ") + error);
        const QJsonObject reflection = error.isEmpty()
            ? reflectionManifest(doc, reflectionObjects, tilesetReflectionSurfaces, reflectionEnvironment)
            : QJsonObject();
        if (!reflection.isEmpty()) manifest.insert(QStringLiteral("reflection"), reflection);
    }

    // Keep an editable, self-contained source for this map alongside runtime data.
    if (map.depthEnabled) manifest.insert("depth", exportDepth(editor, doc));
    QJsonObject source = core::io::buildProjectPayload(editor);
    QJsonArray sourceMaps;
    for (const auto& value : source.value("maps").toArray())
        if (value.toObject().value("id").toString() == doc.id) {
            auto entry=value.toObject();entry.insert("rpgMakerMapId",targetMapId);sourceMaps.append(entry);
        }
    source.insert("maps",sourceMaps);
    source.insert("activeDocIdx",0);
    source.remove("assetDatabase"); source.remove("assetReferences");
    manifest.insert("editorSource",source);
    const QString stagedManifest = stageDir.filePath(QStringLiteral("data/ludoMaps/") + mapJsonName(targetMapId));
    const QString stagedMap = stageDir.filePath(QStringLiteral("data/") + mapJsonName(targetMapId));
    const QString stagedMapInfos = stageDir.filePath(QStringLiteral("data/MapInfos.json"));
    if (error.isEmpty()) writeAtomic(stagedManifest, QJsonDocument(manifest).toJson(QJsonDocument::Compact), error);
    if (error.isEmpty()) writeAtomic(stagedMap, preparedMap, error);
    if (error.isEmpty()) writeAtomic(stagedMapInfos, preparedMapInfos, error);

    if (!error.isEmpty()) {
        trace(QStringLiteral("ERROR stage: ") + error);
        progress.close();
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"), error);
        return false;
    }

    trace(QStringLiteral("phase=staging-files done"));
    // Commit: só começa a alterar o projeto RPG Maker depois que TODA a geração passou.
    const QString finalDataLudo = rpgMakerRoot.filePath(QStringLiteral("data/ludoMaps"));
    const QString finalImgLudo = rpgMakerRoot.filePath(QStringLiteral("img/ludoMaps"));
    const QString finalParallaxes = rpgMakerRoot.filePath(QStringLiteral("img/parallaxes"));
    QDir().mkpath(finalDataLudo + QStringLiteral("/backups/reference"));
    QDir().mkpath(finalImgLudo);
    QDir().mkpath(finalParallaxes);

    const bool mapExisted = QFileInfo::exists(targetMapPath);
    const QString finalReferencePath = QDir(finalParallaxes).filePath(referenceFile);
    const bool referenceExisted = QFileInfo::exists(finalReferencePath);
    const QString backupMapFirst = QDir(finalDataLudo).filePath(QStringLiteral("backups/") + mapJsonName(targetMapId));
    const QString backupMapLast = QDir(finalDataLudo).filePath(QStringLiteral("backups/") + mapJsonName(targetMapId) + QStringLiteral(".before-last-export"));
    const QString backupMapInfosFirst = QDir(finalDataLudo).filePath(QStringLiteral("backups/MapInfos.json.before-ludo"));
    const QString backupMapInfosLast = QDir(finalDataLudo).filePath(QStringLiteral("backups/MapInfos.json.before-last-export"));
    const QString backupReferenceLast = QDir(finalDataLudo).filePath(QStringLiteral("backups/reference/") + referenceFile + QStringLiteral(".before-last-export"));

    if ((mapExisted && (!saveFirstBackup(targetMapPath, backupMapFirst, error) ||
                        !copyAtomic(targetMapPath, backupMapLast, error))) ||
        !saveFirstBackup(mapInfosPath, backupMapInfosFirst, error) ||
        !copyAtomic(mapInfosPath, backupMapInfosLast, error) ||
        (referenceExisted && !copyAtomic(finalReferencePath, backupReferenceLast, error))) {
        progress.close();
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"), error);
        return false;
    }

    trace(QStringLiteral("phase=backups done"));
    bool mapInfosWritten = false;
    bool mapWritten = false;
    bool referenceWritten = false;
    for (const QString& name : generatedImages) {
        if (!copyAtomic(stageDir.filePath(QStringLiteral("img/ludoMaps/") + name),
                        QDir(finalImgLudo).filePath(name), error)) break;
    }
    if (error.isEmpty()) {
        copyAtomic(stagedManifest, QDir(finalDataLudo).filePath(mapJsonName(targetMapId)), error);
    }
    if (error.isEmpty()) {
        referenceWritten = copyAtomic(stageDir.filePath(QStringLiteral("img/parallaxes/") + referenceFile),
                                      finalReferencePath, error);
    }
    if (error.isEmpty()) {
        mapInfosWritten = copyAtomic(stagedMapInfos, mapInfosPath, error);
    }
    if (error.isEmpty()) {
        mapWritten = copyAtomic(stagedMap, targetMapPath, error);
    }

    progress.close();
    if (!error.isEmpty()) {
        QString rollbackDetails;
        auto addRollbackError = [&](const QString& what, const QString& detail) {
            if (!rollbackDetails.isEmpty()) rollbackDetails += QLatin1Char('\n');
            rollbackDetails += what + QStringLiteral(": ") + detail;
        };

        if (mapWritten) {
            QString rollbackError;
            if (mapExisted) {
                if (!copyAtomic(backupMapLast, targetMapPath, rollbackError)) addRollbackError(QStringLiteral("MapXXX"), rollbackError);
            } else if (!QFile::remove(targetMapPath) && QFileInfo::exists(targetMapPath)) {
                addRollbackError(QStringLiteral("MapXXX"), QStringLiteral("não foi possível remover o mapa recém-criado"));
            }
        }
        if (mapInfosWritten) {
            QString rollbackError;
            if (!copyAtomic(backupMapInfosLast, mapInfosPath, rollbackError)) addRollbackError(QStringLiteral("MapInfos.json"), rollbackError);
        }
        if (referenceWritten) {
            if (referenceExisted) {
                QString rollbackError;
                if (!copyAtomic(backupReferenceLast, finalReferencePath, rollbackError)) addRollbackError(QStringLiteral("panorama de referência"), rollbackError);
            } else if (!QFile::remove(finalReferencePath) && QFileInfo::exists(finalReferencePath)) {
                addRollbackError(QStringLiteral("panorama de referência"), QStringLiteral("não foi possível remover o arquivo recém-criado"));
            }
        }
        if (!rollbackDetails.isEmpty()) error += QStringLiteral("\n\nFalhas durante a restauração automática:\n") + rollbackDetails;

        QMessageBox::warning(parent, QStringLiteral("Exportação incompleta"),
            error + QStringLiteral("\n\nO LUDO tentou restaurar automaticamente MapXXX, MapInfos.json e panorama para o estado anterior."));
        return false;
    }

    trace(QStringLiteral("phase=commit done"));
    cleanupStaleMapImages(finalImgLudo, prefix, generatedImages);
    trace(QStringLiteral("phase=complete"));
    traceFile.close();

    const QString mapAction = mapCreated ? QStringLiteral("criado") : QStringLiteral("atualizado");
    const QString reloadHint = editor.rpgMakerEngine == core::RpgMakerEngine::MV
        ? QStringLiteral("\n\nO LUDO pode resetar o RPG Maker MV após a exportação para carregar esta atualização.")
        : (mapCreated || mapInfoCreated
               ? QStringLiteral("\n\nSe o %1 já estava aberto, reabra o projeto para o novo mapa aparecer na árvore.").arg(engineName)
               : QString());
    if (showSuccess) QMessageBox::information(parent, QStringLiteral("Mapa exportado para %1").arg(engineName),
        QStringLiteral("Pronto. O mapa %1 foi %2 diretamente em:\n%3\n\n"
                       "• MapInfos.json sincronizado\n"
                       "• Regiões gravadas na camada nativa do MapXXX\n"
                       "• panorama de referência criado em img/parallaxes/%4\n"
                       "• modo da referência: %8\n"
                       "• o panorama aparece no EDITOR do RPG Maker, mas é ocultado no Playtest\n"
                       "• LudoMapSystem.js e js/plugins.js não foram alterados\n"
                       "• cenário e colisões exportados\n"
                       "• %5 tiles animados mantidos em runtime\n"
                       "• %6 tiles com prioridade 1–5 mantidos na ordenação Y\n"
                       "• eventos existentes do RPG Maker preservados%7")
            .arg(targetMapId).arg(mapAction).arg(rootPath).arg(referenceFile)
            .arg(validation.animated).arg(validation.priority).arg(reloadHint)
            .arg(fitReferenceGrid
                     ? QStringLiteral("ajustado ao grid do RPG Maker (48 px/célula)")
                     : QStringLiteral("1:1 nativo, sem redimensionamento")));    return true;
    } catch (const std::bad_alloc&) {
        if (parent) QMessageBox::critical(parent, QStringLiteral("Exportação interrompida"),
            QStringLiteral("O sistema ficou sem memória durante a exportação."));
    } catch (const std::exception& ex) {
        if (parent) QMessageBox::critical(parent, QStringLiteral("Exportação interrompida"),
            QStringLiteral("A exportação encontrou uma falha inesperada.\n\nDetalhe: ")
                + QString::fromUtf8(ex.what()));
    } catch (...) {
        if (parent) QMessageBox::critical(parent, QStringLiteral("Exportação interrompida"),
            QStringLiteral("A exportação encontrou uma falha inesperada."));
    }
    return false;
}

inline void run(const core::Editor& editor, QWidget* parent)
{
    const core::RpgMakerEngine engine = editor.rpgMakerEngine;
    const QString engineName = core::rpgMakerEngineName(engine);
    const QString engineShort = core::rpgMakerEngineId(engine).toUpper();
    const bool supportsExtraReflection = engine == core::RpgMakerEngine::MZ;
    const QString integrationFolder = engine == core::RpgMakerEngine::MV
                                          ? QStringLiteral("rpg-maker-mv")
                                          : QStringLiteral("rpg-maker-mz");
    const core::MapDoc* source = editor.doc();
    if (!source) {
        QMessageBox::information(parent, QStringLiteral("LUDO → %1").arg(engineShort), QStringLiteral("Abra um mapa primeiro."));
        return;
    }

    const Check validation = check(editor, *source);
    if (!validation.errors.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Revise o mapa antes de exportar"),
                             validation.errors.join(QLatin1Char('\n')));
        return;
    }

    QSettings settings;
    const QString settingsPrefix = QStringLiteral("LudoRpgMaker/") + core::rpgMakerEngineId(engine);
    const QString idKey = settingsPrefix + QStringLiteral("/mapId/") + editor.projectPath + QStringLiteral("/") + source->id;
    const QString rootKey = settingsPrefix + QStringLiteral("/lastProjectRoot");
    const QString referenceFitGridKey = settingsPrefix + QStringLiteral("/referenceFitGrid");

    // IMPORTANT: every QObject/QLayout that becomes owned by the dialog is heap
    // allocated. Previous builds mixed stack-allocated layouts/widgets with Qt
    // parent/layout ownership. On Windows this can surface as 0xC0000374
    // (heap corruption) when the export dialog is destroyed after the operation.
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Exportar direto para %1 — LUDO").arg(engineName));
    dialog.setMinimumWidth(650);
    auto* layout = new QVBoxLayout(&dialog);

    auto* title = new QLabel(QStringLiteral("LUDO MAP EDITOR · %1\nExportação direta para o projeto %1").arg(engineName), &dialog);
    layout->addWidget(title);
    auto* summary = new QLabel(QStringLiteral("%1 × %2 células · tiles %3 × %4 px · %5 animados · %6 com prioridade")
                               .arg(source->map.width).arg(source->map.height)
                               .arg(source->map.tileWidth).arg(source->map.tileHeight)
                               .arg(validation.animated).arg(validation.priority), &dialog);
    layout->addWidget(summary);

    auto* mapId = new QSpinBox(&dialog);
    mapId->setRange(1, 9999);
    mapId->setValue(source->rpgMakerMapId > 0 ? source->rpgMakerMapId : settings.value(idKey, 1).toInt());

    auto* projectRoot = new QLineEdit(&dialog);
    projectRoot->setReadOnly(true);
    projectRoot->setPlaceholderText(QStringLiteral("Selecione a pasta raiz do projeto %1").arg(engineName));
    const QString rememberedRoot = isRpgMakerProjectRoot(editor.rpgMakerProjectRoot, engine)
        ? editor.rpgMakerProjectRoot : settings.value(rootKey).toString();
    if (isRpgMakerProjectRoot(rememberedRoot, engine)) projectRoot->setText(QDir::cleanPath(rememberedRoot));
    auto* chooseRoot = new QPushButton(QStringLiteral("Localizar projeto %1…").arg(engineShort), &dialog);

    auto* rootRow = new QWidget(&dialog);
    auto* rootRowLayout = new QHBoxLayout(rootRow);
    rootRowLayout->setContentsMargins(0, 0, 0, 0);
    rootRowLayout->addWidget(projectRoot, 1);
    rootRowLayout->addWidget(chooseRoot);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Projeto %1:").arg(engineName), rootRow);
    form->addRow(QStringLiteral("ID do mapa no %1:").arg(engineShort), mapId);

    auto* fitReferenceToGrid = new QCheckBox(
        QStringLiteral("Ajustar panorama de referência ao grid do %1 (48 px/célula)").arg(engineName), &dialog);
    fitReferenceToGrid->setChecked(settings.value(referenceFitGridKey, false).toBool());
    fitReferenceToGrid->setToolTip(
        QStringLiteral("Desmarcado: exporta a referência em 1:1 nativo, sem redimensionar. "
                       "Marcado: redimensiona somente a referência para 48 px por célula, facilitando o alinhamento com a grade do RPG Maker."));
    form->addRow(QStringLiteral("Panorama de referência:"), fitReferenceToGrid);
    auto* exportPlugin = new QCheckBox(QStringLiteral("Exportar plugin (LudoMapSystem.js)"), &dialog);
    exportPlugin->setChecked(false);
    exportPlugin->setToolTip(QStringLiteral("Desmarcado: preserva o plugin do jogo. Marcado: copia o arquivo selecionado, com backup e bloqueio de versão mais antiga."));
    form->addRow(QStringLiteral("Plugin:"), exportPlugin);
    auto* pluginSource = new QLineEdit(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("integrations/") + integrationFolder + QStringLiteral("/LudoMapSystem.js")), &dialog);
    auto* choosePlugin = new QPushButton(QStringLiteral("Selecionar…"), &dialog);
    auto* pluginRow = new QWidget(&dialog);
    auto* pluginLayout = new QHBoxLayout(pluginRow); pluginLayout->setContentsMargins(0,0,0,0);
    pluginLayout->addWidget(pluginSource,1); pluginLayout->addWidget(choosePlugin);
    pluginRow->setEnabled(false);
    QObject::connect(exportPlugin,&QCheckBox::toggled,pluginRow,&QWidget::setEnabled);
    QObject::connect(choosePlugin,&QPushButton::clicked,&dialog,[&]() {
        const QString path = QFileDialog::getOpenFileName(&dialog,QStringLiteral("Selecionar LudoMapSystem atualizado"),pluginSource->text(),QStringLiteral("Plugin JavaScript (*.js)"));
        if (!path.isEmpty()) pluginSource->setText(path);
    });
    form->addRow(QStringLiteral("Arquivo do plugin:"),pluginRow);

    auto* exportReflectionPlugin = new QCheckBox(QStringLiteral("Exportar plugin de reflexo (LudoReflectionSystem.js)"), &dialog);
    exportReflectionPlugin->setChecked(false);
    exportReflectionPlugin->setToolTip(QStringLiteral("Copia somente o arquivo LudoReflectionSystem.js. Recurso exclusivo do projeto MZ."));
    form->addRow(QStringLiteral("Reflexo:"), exportReflectionPlugin);
    auto* reflectionPluginSource = new QLineEdit(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("integrations/rpg-maker-mz/LudoReflectionSystem.js")), &dialog);
    auto* chooseReflectionPlugin = new QPushButton(QStringLiteral("Selecionar…"), &dialog);
    auto* reflectionPluginRow = new QWidget(&dialog);
    auto* reflectionPluginLayout = new QHBoxLayout(reflectionPluginRow); reflectionPluginLayout->setContentsMargins(0,0,0,0);
    reflectionPluginLayout->addWidget(reflectionPluginSource,1); reflectionPluginLayout->addWidget(chooseReflectionPlugin);
    reflectionPluginRow->setEnabled(false);
    QObject::connect(exportReflectionPlugin,&QCheckBox::toggled,reflectionPluginRow,&QWidget::setEnabled);
    QObject::connect(chooseReflectionPlugin,&QPushButton::clicked,&dialog,[&]() {
        const QString path = QFileDialog::getOpenFileName(&dialog,QStringLiteral("Selecionar LudoReflectionSystem atualizado"),reflectionPluginSource->text(),QStringLiteral("Plugin JavaScript (*.js)"));
        if (!path.isEmpty()) reflectionPluginSource->setText(path);
    });
    form->addRow(QStringLiteral("Arquivo do reflexo:"),reflectionPluginRow);
    if (engine == core::RpgMakerEngine::MV) {
        exportReflectionPlugin->setVisible(false);
        reflectionPluginRow->setVisible(false);
        if (QWidget* label = form->labelForField(exportReflectionPlugin)) label->setVisible(false);
        if (QWidget* label = form->labelForField(reflectionPluginRow)) label->setVisible(false);
    }
    layout->addLayout(form);

    auto* note = new QLabel(
        QStringLiteral("O LUDO exporta o cenário e também cria o MapXXX no %1 caso ele ainda não exista. Se já existir, ele é atualizado preservando os eventos.\n\nO panorama de referência é exportado em 1:1 nativo por padrão. O LudoMapSystem.js só é copiado ao marcar Exportar plugin. No modo MV, integrações que exigem plugins LUDO extras ficam fora da interface; somente o LudoMapSystem faz parte do fluxo. O js/plugins.js e as configurações do Gerenciador de Plugins são preservados.").arg(engineName),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    if (!validation.collision) {
        auto* warning = new QLabel(QStringLiteral("Aviso: nenhuma colisão marcada. Todo o cenário ficará transitável."), &dialog);
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }

    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QPushButton* save = buttons->button(QDialogButtonBox::Save);
    save->setText(QStringLiteral("Exportar direto para o projeto"));
    layout->addWidget(buttons);

    auto refreshStatus = [&]() {
        QString reason;
        const bool valid = isRpgMakerProjectRoot(projectRoot->text(), engine, &reason);
        if (valid) {
            const QString mapFile = QDir(projectRoot->text()).filePath(QStringLiteral("data/") + mapJsonName(mapId->value()));
            if (QFileInfo::exists(mapFile)) {
                status->setText(QStringLiteral("Projeto %1 válido. O mapa %2 será atualizado e receberá o panorama de referência.").arg(engineShort).arg(mapId->value()));
            } else {
                status->setText(QStringLiteral("Projeto %1 válido. O mapa %2 ainda não existe: o LUDO vai criá-lo automaticamente e registrar em MapInfos.json.").arg(engineShort).arg(mapId->value()));
            }
            save->setEnabled(true);
        } else {
            status->setText(projectRoot->text().isEmpty()
                            ? QStringLiteral("Selecione a pasta que contém o arquivo %1.").arg(core::rpgMakerProjectExtension(engine))
                            : reason);
            save->setEnabled(false);
        }
    };

    QObject::connect(chooseRoot, &QPushButton::clicked, &dialog, [&]() {
        const QString initial = projectRoot->text().isEmpty()
            ? settings.value(rootKey, QDir::homePath()).toString()
            : projectRoot->text();
        const QString path = QFileDialog::getExistingDirectory(&dialog,
            QStringLiteral("Selecione a pasta raiz do projeto %1").arg(engineName), initial);
        if (!path.isEmpty()) projectRoot->setText(QDir::cleanPath(path));
        refreshStatus();
    });
    QObject::connect(mapId, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) { refreshStatus(); });
    QObject::connect(save, &QPushButton::clicked, &dialog, [&]() {
        refreshStatus();
        if (save->isEnabled()) dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refreshStatus();
    if (dialog.exec() != QDialog::Accepted) return;

    try {
    const QString rootPath = QDir::cleanPath(projectRoot->text());
    const QDir rpgMakerRoot(rootPath);
    const int targetMapId = mapId->value();
    const bool fitReferenceGrid = fitReferenceToGrid->isChecked();
    const QString prefix = mapPrefix(targetMapId);
    const QString targetMapPath = rpgMakerRoot.filePath(QStringLiteral("data/") + mapJsonName(targetMapId));

    // Log próprio, síncrono e com flush. O log Qt do launcher pode ficar em 0 KB
    // quando o Windows encerra o processo por corrupção de heap antes do flush.
    // Este arquivo permite identificar a última fase concluída mesmo nesse caso.
    const QString traceDir = rpgMakerRoot.filePath(QStringLiteral("data/ludoMaps"));
    QDir().mkpath(traceDir);
    QFile traceFile(QDir(traceDir).filePath(QStringLiteral("LUDO_EXPORT_TRACE.txt")));
    traceFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    auto trace = [&](const QString& line) {
        if (!traceFile.isOpen()) return;
        traceFile.write((line + QLatin1Char('\n')).toUtf8());
        traceFile.flush();
    };
    trace(QStringLiteral("BEGIN editor=%1 engine=%2 map=%3")
              .arg(QString::fromLatin1(core::version::Editor))
              .arg(engineShort)
              .arg(targetMapId));
    trace(QStringLiteral("referenceMode=%1").arg(fitReferenceGrid ? QStringLiteral("rpgmaker-grid-48") : QStringLiteral("native-1to1")));

    trace(QStringLiteral("phase=prepare-json"));
    const QString mapInfosPath = rpgMakerRoot.filePath(QStringLiteral("data/MapInfos.json"));
    const QString referenceBase = referenceParallaxBaseName(targetMapId);
    const QString referenceFile = referenceParallaxFileName(targetMapId);
    QByteArray preparedMap;
    QByteArray preparedMapInfos;
    bool mapCreated = false;
    bool mapInfoCreated = false;
    QString error;
    if (!makePreparedMapJson(targetMapPath, *source, referenceBase, preparedMap, mapCreated, error) ||
        !makePreparedMapInfosJson(mapInfosPath, targetMapId, *source, preparedMapInfos, mapInfoCreated, error)) {
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"), error);
        return;
    }
    applyEditorMapInfoHierarchy(editor, *source, targetMapId, preparedMapInfos);

    trace(QStringLiteral("phase=snapshot"));
    const core::MapDoc doc = *source;
    const core::MapInfo& map = doc.map;
    trace(QStringLiteral("regions=%1 authored=%2")
              .arg(doc.rpgMakerRegions.size())
              .arg(doc.rpgMakerRegionsAuthored ? QStringLiteral("yes") : QStringLiteral("preserve-rpgmaker")));
    const RuntimeOrdering runtimeOrdering = buildRuntimeOrdering(editor, doc);
    QJsonArray dynamic;
    QJsonArray reflectionObjects;
    QSet<int> dynamicTilesets;
    collectDynamic(editor, doc, targetMapId, runtimeOrdering, dynamic, dynamicTilesets);
    if (supportsExtraReflection)
        collectReflectionObjects(editor, doc, targetMapId, reflectionObjects, dynamicTilesets);
    trace(QStringLiteral("phase=dynamic done count=%1 reflectionObjects=%2 atlases=%3")
              .arg(dynamic.size()).arg(reflectionObjects.size()).arg(dynamicTilesets.size()));

    QTemporaryDir stage(rpgMakerRoot.filePath(QStringLiteral(".ludo-") + core::rpgMakerEngineId(engine) + QStringLiteral("-export-XXXXXX")));
    if (!stage.isValid()) {
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"),
                             QStringLiteral("Não foi possível criar a área temporária dentro do projeto %1. Confira as permissões.").arg(engineName));
        return;
    }
    QDir stageDir(stage.path());
    if (!stageDir.mkpath(QStringLiteral("data/ludoMaps")) ||
        !stageDir.mkpath(QStringLiteral("img/ludoMaps")) ||
        !stageDir.mkpath(QStringLiteral("img/parallaxes"))) {
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"),
                             QStringLiteral("Não foi possível preparar as pastas temporárias da exportação."));
        return;
    }

    trace(QStringLiteral("phase=stage-ready"));
    QJsonArray chunks;
    QVector<int> collision(map.width * map.height, 0);
    collectCollision(editor, doc, collision);
    trace(QStringLiteral("phase=collision done cells=%1").arg(collision.size()));

    const int chunkCount = ((map.pixelWidth() + 511) / 512) * ((map.pixelHeight() + 511) / 512);
    const int progressMax = qMax(1, chunkCount + dynamicTilesets.size() + 1);
    // Top-level e sem QObject parent: evita qualquer relação de ownership com o
    // MainWindow enquanto a exportação pesada está em andamento.
    QProgressDialog progress(QStringLiteral("Exportando mapa para %1…").arg(engineName),
                             QString(), 0, progressMax, nullptr);
    progress.setWindowTitle(QStringLiteral("LUDO → %1").arg(engineName));
    // Não bombeamos o event loop inteiro durante a exportação. Na 0.3.1,
    // QApplication::processEvents() permitia reentrância no MainWindow enquanto
    // o mapa estava sendo renderizado/gravado.
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.setAutoReset(false);
    progress.setAutoClose(false);
    progress.show();
    progress.repaint();

    int done = 0;
    QSet<QString> generatedImages;

    trace(QStringLiteral("phase=reference begin file=%1").arg(referenceFile));
    QImage referenceImage;
    if (!renderReferenceParallax(editor, doc, fitReferenceGrid, referenceImage, error) ||
        !referenceImage.save(stageDir.filePath(QStringLiteral("img/parallaxes/") + referenceFile), "PNG")) {
        if (error.isEmpty()) error = QStringLiteral("Falha ao gravar o panorama de referência ") + referenceFile;
    }
    referenceImage = QImage(); // libera a maior alocação antes de renderizar os chunks.
    if (error.isEmpty()) {
        trace(QStringLiteral("phase=reference done"));
        progress.setValue(++done);
        progress.repaint();
    }

    for (int y = 0; y < map.pixelHeight() && error.isEmpty(); y += 512) {
        for (int x = 0; x < map.pixelWidth() && error.isEmpty(); x += 512) {
            const QRect rect(x, y, qMin(512, map.pixelWidth() - x), qMin(512, map.pixelHeight() - y));
            trace(QStringLiteral("phase=chunk x=%1 y=%2 w=%3 h=%4").arg(x).arg(y).arg(rect.width()).arg(rect.height()));

            for (int renderOrder = 0; renderOrder < runtimeOrdering.bandCount && error.isEmpty(); ++renderOrder) {
                for (int level = 0; level < (map.depthEnabled ? 2 : 1); ++level) for (bool above : {false, true}) {
                    const QImage image = renderStatic(editor, doc, runtimeOrdering, renderOrder, rect, above, 0, map.depthEnabled ? level : -1);
                    if (image.isNull()) {
                        error = QStringLiteral("Memória insuficiente para gerar o cenário.");
                        break;
                    }
                    if ((above || level > 0 || renderOrder > 0) && emptyImage(image)) continue;
                    const QString plane = above ? QStringLiteral("above") : QStringLiteral("below");
                    const QString name = QStringLiteral("%1_%2_%3_%4_L%5_R%6.png")
                                             .arg(prefix).arg(x).arg(y).arg(plane).arg(level).arg(renderOrder);
                    if (!image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/") + name), "PNG")) {
                        error = QStringLiteral("Falha ao gravar a imagem ") + name;
                        break;
                    }
                    generatedImages.insert(name);
                    chunks.append(QJsonObject{
                        {QStringLiteral("file"), name},
                        {QStringLiteral("plane"), plane},
                        {QStringLiteral("level"), level},
                        {QStringLiteral("renderOrder"), renderOrder},
                        {QStringLiteral("x"), x},
                        {QStringLiteral("y"), y},
                        {QStringLiteral("width"), rect.width()},
                        {QStringLiteral("height"), rect.height()}
                    });
                }
            }
            progress.setValue(++done);
            progress.repaint();
        }
    }

    trace(QStringLiteral("phase=chunks done count=%1").arg(chunks.size()));
    for (int tilesetIdx : dynamicTilesets) {
        if (!error.isEmpty()) break;
        const core::Tileset* ts = editor.tilesetAt(tilesetIdx);
        if (!ts || ts->image.isNull()) {
            error = QStringLiteral("Tileset dinâmico inválido durante a exportação.");
            break;
        }
        const QString name = atlasName(targetMapId, tilesetIdx);
        if (!ts->image.save(stageDir.filePath(QStringLiteral("img/ludoMaps/") + name), "PNG")) {
            error = QStringLiteral("Falha ao exportar o atlas dinâmico ") + name;
            break;
        }
        generatedImages.insert(name);
        progress.setValue(++done);
        progress.repaint();
    }

    trace(QStringLiteral("phase=atlases done"));
    QJsonArray parallaxLayers;
    if(error.isEmpty()&&!exportParallaxLayers(editor,doc,targetMapId,stageDir,parallaxLayers,generatedImages,error))
        trace(QStringLiteral("ERROR parallax-layers: ")+error);
    QJsonArray cells;
    for (int bits : collision) cells.append(bits);
    QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("ludo-map")},
        {QStringLiteral("version"), 5},
        {QStringLiteral("exporter"), QStringLiteral("LUDO Map Editor Runtime Contract v5")},
        {QStringLiteral("mapId"), targetMapId},
        {QStringLiteral("width"), map.width},
        {QStringLiteral("height"), map.height},
        {QStringLiteral("tileWidth"), map.tileWidth},
        {QStringLiteral("tileHeight"), map.tileHeight},
        {QStringLiteral("collision"), cells},
        {QStringLiteral("regionCount"), doc.rpgMakerRegions.size()},
        {QStringLiteral("regionsAuthored"), doc.rpgMakerRegionsAuthored},
        {QStringLiteral("chunks"), chunks},
        {QStringLiteral("dynamic"), dynamic},
        {QStringLiteral("renderOrderCount"), runtimeOrdering.bandCount},
        {QStringLiteral("textureFiltering"), QStringLiteral("nearest")}
    };
    if(!parallaxLayers.isEmpty()){manifest.insert(QStringLiteral("parallaxLayers"),parallaxLayers);manifest.insert(QStringLiteral("backgroundColor"),map.background.name(QColor::HexRgb));}
    const QJsonArray mapVariations = mapVariationManifest(editor, doc);
    if (!mapVariations.isEmpty()) {
        const QString variationBaseId = doc.variationBaseId.isEmpty() ? doc.id : doc.variationBaseId;
        const core::MapDoc* variationBase = editor.mapById(variationBaseId);
        manifest.insert(QStringLiteral("variations"), mapVariations);
        if (variationBase && variationBase->rpgMakerMapId > 0)
            manifest.insert(QStringLiteral("variationBaseMapId"), variationBase->rpgMakerMapId);
        if (!doc.variationBaseId.isEmpty())
            manifest.insert(QStringLiteral("variationName"), doc.variationName);
    }
    if (supportsExtraReflection) {
        QJsonArray tilesetReflectionSurfaces;
        if (error.isEmpty() && !exportTilesetReflectionMasks(editor, doc, targetMapId, stageDir, tilesetReflectionSurfaces, generatedImages, error))
            trace(QStringLiteral("ERROR reflection-masks: ") + error);
        if(error.isEmpty()&&!exportAuthoredReflectionLayers(editor,doc,targetMapId,stageDir,tilesetReflectionSurfaces,generatedImages,error))
            trace(QStringLiteral("ERROR reflection-layers: ")+error);
        QJsonObject reflectionEnvironment;
        if (error.isEmpty() && !exportReflectionEnvironment(doc, targetMapId, stageDir, reflectionEnvironment, generatedImages, error))
            trace(QStringLiteral("ERROR reflection-environment: ") + error);
        const QJsonObject reflection = error.isEmpty()
            ? reflectionManifest(doc, reflectionObjects, tilesetReflectionSurfaces, reflectionEnvironment)
            : QJsonObject();
        if (!reflection.isEmpty()) manifest.insert(QStringLiteral("reflection"), reflection);
    }

    // Keep an editable, self-contained source for this map alongside runtime data.
    if (map.depthEnabled) manifest.insert("depth", exportDepth(editor, doc));
    QJsonObject source = core::io::buildProjectPayload(editor);
    QJsonArray sourceMaps;
    for (const auto& value : source.value("maps").toArray())
        if (value.toObject().value("id").toString() == doc.id) {
            auto entry=value.toObject();entry.insert("rpgMakerMapId",targetMapId);sourceMaps.append(entry);
        }
    source.insert("maps",sourceMaps);
    source.insert("activeDocIdx",0);
    source.remove("assetDatabase"); source.remove("assetReferences");
    manifest.insert("editorSource",source);
    const QString stagedManifest = stageDir.filePath(QStringLiteral("data/ludoMaps/") + mapJsonName(targetMapId));
    const QString stagedMap = stageDir.filePath(QStringLiteral("data/") + mapJsonName(targetMapId));
    const QString stagedMapInfos = stageDir.filePath(QStringLiteral("data/MapInfos.json"));
    if (error.isEmpty()) writeAtomic(stagedManifest, QJsonDocument(manifest).toJson(QJsonDocument::Compact), error);
    if (error.isEmpty()) writeAtomic(stagedMap, preparedMap, error);
    if (error.isEmpty()) writeAtomic(stagedMapInfos, preparedMapInfos, error);

    if (!error.isEmpty()) {
        trace(QStringLiteral("ERROR stage: ") + error);
        progress.close();
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"), error);
        return;
    }

    trace(QStringLiteral("phase=staging-files done"));
    // Commit: só começa a alterar o projeto RPG Maker depois que TODA a geração passou.
    const QString finalDataLudo = rpgMakerRoot.filePath(QStringLiteral("data/ludoMaps"));
    const QString finalImgLudo = rpgMakerRoot.filePath(QStringLiteral("img/ludoMaps"));
    const QString finalParallaxes = rpgMakerRoot.filePath(QStringLiteral("img/parallaxes"));
    QDir().mkpath(finalDataLudo + QStringLiteral("/backups/reference"));
    QDir().mkpath(finalImgLudo);
    QDir().mkpath(finalParallaxes);

    const bool mapExisted = QFileInfo::exists(targetMapPath);
    const QString finalReferencePath = QDir(finalParallaxes).filePath(referenceFile);
    const bool referenceExisted = QFileInfo::exists(finalReferencePath);
    const QString backupMapFirst = QDir(finalDataLudo).filePath(QStringLiteral("backups/") + mapJsonName(targetMapId));
    const QString backupMapLast = QDir(finalDataLudo).filePath(QStringLiteral("backups/") + mapJsonName(targetMapId) + QStringLiteral(".before-last-export"));
    const QString backupMapInfosFirst = QDir(finalDataLudo).filePath(QStringLiteral("backups/MapInfos.json.before-ludo"));
    const QString backupMapInfosLast = QDir(finalDataLudo).filePath(QStringLiteral("backups/MapInfos.json.before-last-export"));
    const QString backupReferenceLast = QDir(finalDataLudo).filePath(QStringLiteral("backups/reference/") + referenceFile + QStringLiteral(".before-last-export"));

    if ((mapExisted && (!saveFirstBackup(targetMapPath, backupMapFirst, error) ||
                        !copyAtomic(targetMapPath, backupMapLast, error))) ||
        !saveFirstBackup(mapInfosPath, backupMapInfosFirst, error) ||
        !copyAtomic(mapInfosPath, backupMapInfosLast, error) ||
        (referenceExisted && !copyAtomic(finalReferencePath, backupReferenceLast, error))) {
        progress.close();
        QMessageBox::warning(parent, QStringLiteral("Exportação não concluída"), error);
        return;
    }

    trace(QStringLiteral("phase=backups done"));
    bool mapInfosWritten = false;
    bool mapWritten = false;
    bool referenceWritten = false;
    for (const QString& name : generatedImages) {
        if (!copyAtomic(stageDir.filePath(QStringLiteral("img/ludoMaps/") + name),
                        QDir(finalImgLudo).filePath(name), error)) break;
    }
    if (error.isEmpty()) {
        copyAtomic(stagedManifest, QDir(finalDataLudo).filePath(mapJsonName(targetMapId)), error);
    }
    if (error.isEmpty()) {
        referenceWritten = copyAtomic(stageDir.filePath(QStringLiteral("img/parallaxes/") + referenceFile),
                                      finalReferencePath, error);
    }
    if (error.isEmpty()) {
        mapInfosWritten = copyAtomic(stagedMapInfos, mapInfosPath, error);
    }
    if (error.isEmpty()) {
        mapWritten = copyAtomic(stagedMap, targetMapPath, error);
    }

    progress.close();
    if (!error.isEmpty()) {
        QString rollbackDetails;
        auto addRollbackError = [&](const QString& what, const QString& detail) {
            if (!rollbackDetails.isEmpty()) rollbackDetails += QLatin1Char('\n');
            rollbackDetails += what + QStringLiteral(": ") + detail;
        };

        if (mapWritten) {
            QString rollbackError;
            if (mapExisted) {
                if (!copyAtomic(backupMapLast, targetMapPath, rollbackError)) addRollbackError(QStringLiteral("MapXXX"), rollbackError);
            } else if (!QFile::remove(targetMapPath) && QFileInfo::exists(targetMapPath)) {
                addRollbackError(QStringLiteral("MapXXX"), QStringLiteral("não foi possível remover o mapa recém-criado"));
            }
        }
        if (mapInfosWritten) {
            QString rollbackError;
            if (!copyAtomic(backupMapInfosLast, mapInfosPath, rollbackError)) addRollbackError(QStringLiteral("MapInfos.json"), rollbackError);
        }
        if (referenceWritten) {
            if (referenceExisted) {
                QString rollbackError;
                if (!copyAtomic(backupReferenceLast, finalReferencePath, rollbackError)) addRollbackError(QStringLiteral("panorama de referência"), rollbackError);
            } else if (!QFile::remove(finalReferencePath) && QFileInfo::exists(finalReferencePath)) {
                addRollbackError(QStringLiteral("panorama de referência"), QStringLiteral("não foi possível remover o arquivo recém-criado"));
            }
        }
        if (!rollbackDetails.isEmpty()) error += QStringLiteral("\n\nFalhas durante a restauração automática:\n") + rollbackDetails;

        QMessageBox::warning(parent, QStringLiteral("Exportação incompleta"),
            error + QStringLiteral("\n\nO LUDO tentou restaurar automaticamente MapXXX, MapInfos.json e panorama para o estado anterior."));
        return;
    }

    trace(QStringLiteral("phase=commit done"));
    cleanupStaleMapImages(finalImgLudo, prefix, generatedImages);
    settings.setValue(idKey, targetMapId);
    settings.setValue(rootKey, rootPath);
    settings.setValue(referenceFitGridKey, fitReferenceGrid);
    trace(QStringLiteral("phase=complete"));
    traceFile.close();

    QString pluginResult = QStringLiteral("LudoMapSystem.js preservado (Exportar plugin desmarcado)");
    if (exportPlugin->isChecked()) {
        QString pluginError;
        if (!exportSelectedPlugin(pluginSource->text(),rootPath,pluginError,engine)) {
            QMessageBox::warning(parent,QStringLiteral("Mapa exportado; plugin não atualizado"),pluginError);
            pluginResult = QStringLiteral("LudoMapSystem.js não atualizado; consulte o aviso");
        } else pluginResult = QStringLiteral("LudoMapSystem.js exportado / já atualizado");
    }
    QString reflectionPluginResult = supportsExtraReflection
        ? QStringLiteral("LudoReflectionSystem.js preservado (exportação desmarcada)")
        : QStringLiteral("plugins extras desativados no modo MV");
    if (supportsExtraReflection && exportReflectionPlugin->isChecked()) {
        QString pluginError;
        if (!exportSelectedReflectionPlugin(reflectionPluginSource->text(),rootPath,pluginError)) {
            QMessageBox::warning(parent,QStringLiteral("Mapa exportado; plugin de reflexo não atualizado"),pluginError);
            reflectionPluginResult = QStringLiteral("LudoReflectionSystem.js não atualizado; consulte o aviso");
        } else reflectionPluginResult = QStringLiteral("LudoReflectionSystem.js exportado / já atualizado");
    }
    pluginResult += QStringLiteral("; ") + reflectionPluginResult;
    const QString mapAction = mapCreated ? QStringLiteral("criado") : QStringLiteral("atualizado");
    const QString reloadHint = engine == core::RpgMakerEngine::MV
        ? QStringLiteral("\n\nO LUDO pode resetar o RPG Maker MV na próxima etapa para carregar esta atualização.")
        : (mapCreated || mapInfoCreated
               ? QStringLiteral("\n\nSe o %1 já estava aberto, reabra o projeto para o novo mapa aparecer na árvore.").arg(engineName)
               : QString());
    QMessageBox::information(parent, QStringLiteral("Mapa exportado para %1").arg(engineName),
        QStringLiteral("Pronto. O mapa %1 foi %2 diretamente em:\n%3\n\n"
                       "• MapInfos.json sincronizado\n"
                       "• Regiões gravadas na camada nativa do MapXXX\n"
                       "• panorama de referência criado em img/parallaxes/%4\n"
                       "• modo da referência: %8\n"
                       "• o panorama aparece no EDITOR do RPG Maker, mas é ocultado no Playtest\n"
                       "• %9; js/plugins.js preservado\n"
                       "• cenário e colisões exportados\n"
                       "• %5 tiles animados mantidos em runtime\n"
                       "• %6 tiles com prioridade 1–5 mantidos na ordenação Y\n"
                       "• eventos existentes do RPG Maker preservados%7")
            .arg(targetMapId).arg(mapAction).arg(rootPath).arg(referenceFile)
            .arg(validation.animated).arg(validation.priority).arg(reloadHint)
            .arg(fitReferenceGrid
                     ? QStringLiteral("ajustado ao grid do RPG Maker (48 px/célula)")
                     : QStringLiteral("1:1 nativo, sem redimensionamento")).arg(pluginResult));
    rpgMakerMvReset::askAndReset(parent, engine, rootPath);
    } catch (const std::bad_alloc&) {
        QMessageBox::critical(parent, QStringLiteral("Exportação interrompida"),
            QStringLiteral("O sistema ficou sem memória durante a exportação. Nenhum arquivo em preparação deve substituir o projeto sem passar pela etapa transacional."));
    } catch (const std::exception& ex) {
        QMessageBox::critical(parent, QStringLiteral("Exportação interrompida"),
            QStringLiteral("A exportação encontrou uma falha inesperada e foi encerrada sem fechar o LUDO Map Editor.\n\nDetalhe: ")
                + QString::fromUtf8(ex.what()));
    } catch (...) {
        QMessageBox::critical(parent, QStringLiteral("Exportação interrompida"),
            QStringLiteral("A exportação encontrou uma falha inesperada e foi encerrada sem fechar o LUDO Map Editor."));
    }
}

} // namespace ui::rpgMaker
