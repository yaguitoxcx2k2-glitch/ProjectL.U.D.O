#include "Editor.h"
#include "MapWorkflow.h"
#include "PaintOps.h"
#include "ResourceManager.h"
#include "Renderer.h"
#include "TilesetOps.h"
#include "TilesetCatalog.h"

#include <QRandomGenerator>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <utility>

namespace core {

Editor& editor()
{
    static Editor e;
    return e;
}

Editor::Editor(QObject* parent)
    : QObject(parent)
{
    // Revisão monotônica usada por caches de preview/exportação. Como todo
    // caminho oficial de edição já emite mapChanged(), não precisamos espalhar
    // incrementos manuais por PaintOps, layers e tilesets.
    connect(this, &Editor::mapChanged, this, [this] {
        ++m_mapRevision;
        if (m_mapRevision == 0) ++m_mapRevision; // zero fica reservado a cache inválido
    });
    m_resources = new ResourceManager(this);
    assetDatabase.metadataChanged = [this](const QString& path) {
        markDirty();resources().notifyAssetsChanged({path});
    };
    newProject();
}

ResourceManager& Editor::resources() { return *m_resources; }
const ResourceManager& Editor::resources() const { return *m_resources; }

QString Editor::projectRoot() const
{
    return projectPath.isEmpty() ? QString() : QFileInfo(projectPath).absolutePath();
}

QString Editor::assetsRoot() const
{
    const QString raiz = projectRoot();
    return raiz.isEmpty() ? QString() : QDir(raiz).filePath(QStringLiteral("Assets"));
}

QString Editor::projectRelativePath(const QString& absolutePath) const
{
    const QString raiz = projectRoot();
    if (raiz.isEmpty() || absolutePath.isEmpty()) return absolutePath;
    return QDir(raiz).relativeFilePath(QFileInfo(absolutePath).absoluteFilePath());
}

// --------------------------------------------------------------- documentos
MapDoc* Editor::doc()
{
    if (activeDocIdx < 0 || activeDocIdx >= docs.size()) return nullptr;
    return &docs[activeDocIdx];
}
const MapDoc* Editor::doc() const
{
    if (activeDocIdx < 0 || activeDocIdx >= docs.size()) return nullptr;
    return &docs[activeDocIdx];
}

int Editor::mapIndexById(const QString& id) const
{
    for (int i = 0; i < docs.size(); ++i)
        if (docs[i].id == id) return i;
    return -1;
}

MapDoc* Editor::mapById(const QString& id)
{
    const int i = mapIndexById(id);
    return i >= 0 ? &docs[i] : nullptr;
}

const MapDoc* Editor::mapById(const QString& id) const
{
    const int i = mapIndexById(id);
    return i >= 0 ? &docs[i] : nullptr;
}

MapInfo& Editor::mapInfo()
{
    static MapInfo fallback;
    MapDoc* d = doc();
    return d ? d->map : fallback;
}
const MapInfo& Editor::mapInfo() const
{
    static const MapInfo fallback;
    const MapDoc* d = doc();
    return d ? d->map : fallback;
}

QVector<LayerPtr>& Editor::layers()
{
    static QVector<LayerPtr> fallback;
    MapDoc* d = doc();
    return d ? d->layers : fallback;
}
const QVector<LayerPtr>& Editor::layers() const
{
    static const QVector<LayerPtr> fallback;
    const MapDoc* d = doc();
    return d ? d->layers : fallback;
}

void Editor::newProject()
{
    docs.clear();
    activeDocIdx = -1;
    tilesets.clear();
    autotiles.clear();
    wangSets.clear();
    starTiles.clear();
    randomPool.clear();
    savedStamps.clear();
    recentTiles.clear();
    assetDatabase.clear();
    session.resetForProject();
    projectName = QStringLiteral("Meu projeto");
    projectId = idGen();
    projectPath.clear();
    rpgMakerEngine = RpgMakerEngine::MZ;
    rpgMakerProjectRoot.clear();
    rpgMakerStructurePending = false;
    rpgMakerPendingDeletedMapIds.clear();
    projectDirty = false;

    MapInfo info;
    addMapDoc(QStringLiteral("Mapa 1"), info, true);

    // Duas camadas iniciais para autoria visual. O mapa inicial do jogo é
    // responsabilidade do próprio RPG Maker selecionado.
    MapDoc* d = doc();
    if (d) {
        const int cols = info.pixelWidth() / info.tileWidth;
        const int rows = info.pixelHeight() / info.tileHeight;
        d->layers.push_back(makeTileLayer(QStringLiteral("Chão"), info.tileWidth, info.tileHeight, cols, rows));
        d->layers.push_back(makeTileLayer(QStringLiteral("Decoração"), info.tileWidth, info.tileHeight, cols, rows));
        d->activeLayerIdx = 0;
        d->activeLayerId = d->layers.first()->id;
        session.selectedLayerId = d->activeLayerId;
        d->dirty = false;
    }
    emit docsChanged();
    emit layersChanged();
    emit tilesetsChanged();
    emit wangChanged();
    emit selectionChanged();
    if (m_resources) m_resources->notifyAssetsChanged();
    emit mapChanged();
    emit projectChanged();
}

QString Editor::uniqueMapName(const QString& base) const
{
    QString name = base;
    int n = 1;
    bool clash = true;
    while (clash) {
        clash = false;
        for (const MapDoc& d : docs)
            if (d.name == name) { clash = true; break; }
        if (clash) name = QStringLiteral("%1 %2").arg(base).arg(++n);
    }
    return name;
}

int Editor::addMapDoc(const QString& name, const MapInfo& info, bool activate)
{
    MapDoc d;
    d.name = name.isEmpty() ? uniqueMapName(QStringLiteral("Mapa")) : name;
    d.map = info;
    d.activeLayerIdx = -1;
    docs.push_back(d);
    const int idx = docs.size() - 1;
    if (activate) activeDocIdx = idx;
    emit docsChanged();
    return idx;
}

void Editor::switchDoc(int idx)
{
    if (idx < 0 || idx >= docs.size() || idx == activeDocIdx) return;
    activeDocIdx = idx;
    session.selectedObjectId.clear();
    session.selectedObjectIds.clear();
    session.authoringContext = AuthoringContext::Map;
    session.selectedLayerId = activeLayer() ? activeLayer()->id : QString();
    session.selectedLayerIds = session.selectedLayerId.isEmpty()
        ? QSet<QString>() : QSet<QString>{session.selectedLayerId};
    emit docsChanged();
    emit layersChanged();
    emit mapChanged();
    emit historyChanged();
}


// ------------------------------------------------------------------ camadas
QVector<LayerPtr> Editor::flatLayers() const
{
    return flattenRenderableLayers(layers());
}

int Editor::activeLayerIdx() const
{
    const MapDoc* d = doc();
    if (!d) return -1;
    const QVector<LayerPtr> flat = flatLayers();
    if (flat.isEmpty()) return -1;

    // Grupo/Pasta e apenas um contêiner organizacional. Quando ele está
    // selecionado não existe alvo de pintura ativo: isso impede que o pincel,
    // Autotile, borracha ou ferramentas de objeto continuem editando a última
    // Tile Layer selecionada por baixo do grupo. O ID operacional anterior é
    // preservado no documento e volta a valer quando uma camada real é escolhida.
    if (!session.selectedLayerId.isEmpty()) {
        const LayerPtr selected = findNode(session.selectedLayerId);
        if (selected && selected->type == LayerType::Group) return -1;
    }

    // RC2.52: o ID estavel vence o indice legado. Isso impede que um drag/drop
    // mude silenciosamente a camada de pintura apenas porque a ordem mudou.
    if (!d->activeLayerId.isEmpty())
        for (int i = 0; i < flat.size(); ++i)
            if (flat[i] && flat[i]->id == d->activeLayerId) return i;
    return clampi(d->activeLayerIdx, 0, flat.size() - 1);
}

LayerPtr Editor::activeLayer() const
{
    const QVector<LayerPtr> flat = flatLayers();
    const int idx = activeLayerIdx();
    if (idx < 0 || idx >= flat.size()) return LayerPtr();
    return flat[idx];
}

void Editor::setActiveLayerIdx(int idx)
{
    MapDoc* d = doc();
    if (!d) return;
    const QVector<LayerPtr> flat = flatLayers();
    if (flat.isEmpty()) {
        d->activeLayerIdx = -1;
        d->activeLayerId.clear();
        session.selectedLayerId.clear();
        session.selectedLayerIds.clear();
    } else {
        d->activeLayerIdx = clampi(idx, 0, flat.size() - 1);
        d->activeLayerId = flat[d->activeLayerIdx] ? flat[d->activeLayerIdx]->id : QString();
        // Uma mudança explícita do alvo operacional também move a seleção
        // visual. Groups são a exceção e entram por setSelectedLayerById().
        session.selectedLayerId = d->activeLayerId;
        session.selectedLayerIds = d->activeLayerId.isEmpty()
            ? QSet<QString>() : QSet<QString>{d->activeLayerId};
    }
    emit layersChanged();
    emit selectionChanged();
}

void Editor::setActiveLayerById(const QString& id)
{
    const QVector<LayerPtr> flat = flatLayers();
    for (int i = 0; i < flat.size(); ++i)
        if (flat[i] && flat[i]->id == id) { setActiveLayerIdx(i); return; }
}

LayerPtr Editor::selectedLayer() const
{
    if (!session.selectedLayerId.isEmpty()) {
        const LayerPtr selected = findNode(session.selectedLayerId);
        if (selected) return selected;
    }
    return activeLayer();
}

QVector<LayerPtr> Editor::selectedLayers() const
{
    QVector<LayerPtr> out;
    QSet<QString> ids = session.selectedLayerIds;
    if (!session.selectedLayerId.isEmpty()) ids.insert(session.selectedLayerId);
    std::function<void(const QVector<LayerPtr>&)> collect;
    collect = [&](const QVector<LayerPtr>& nodes) {
        for (const LayerPtr& layer : nodes) {
            if (!layer) continue;
            if (ids.contains(layer->id)) out.push_back(layer);
            if (!layer->children.isEmpty()) collect(layer->children);
        }
    };
    collect(layers());
    if (out.isEmpty()) {
        if (const LayerPtr primary = selectedLayer()) out.push_back(primary);
    }
    return out;
}

void Editor::setSelectedLayerById(const QString& id)
{
    const LayerPtr node = findNode(id);
    if (!node) return;
    session.selectedLayerId = node->id;
    session.selectedLayerIds = QSet<QString>{node->id};
    if (!session.selectedMaskLayerId.isEmpty() && session.selectedMaskLayerId != node->id)
        session.selectedMaskLayerId.clear();
    // Grupo/Pasta é apenas seleção estrutural. Ele nunca herda o alvo de
    // pintura/objeto da última camada usada. Assim o Inspector e a toolbar
    // entram no contexto da pasta, em vez de parecer que a pasta é uma Tile
    // Layer ou Object Layer disfarçada.
    if (node->type != LayerType::Group) {
        setActiveLayerById(node->id);
        return;
    }
    session.selectedObjectId.clear();
    session.selectedObjectIds.clear();
    emit selectionChanged();
}

void Editor::setSelectedLayerIds(const QSet<QString>& ids, const QString& primaryId)
{
    QSet<QString> valid;
    for (const QString& id : ids) if (findNode(id)) valid.insert(id);
    QString primary = primaryId;
    if (primary.isEmpty() || !valid.contains(primary)) {
        if (valid.contains(session.selectedLayerId)) primary = session.selectedLayerId;
        else if (!valid.isEmpty()) primary = *valid.constBegin();
    }
    if (primary.isEmpty()) return;

    const LayerPtr node = findNode(primary);
    if (!node) return;
    session.selectedLayerIds = valid;
    session.selectedLayerIds.insert(primary);
    session.selectedLayerId = primary;
    if (!session.selectedMaskLayerId.isEmpty() && !session.selectedLayerIds.contains(session.selectedMaskLayerId))
        session.selectedMaskLayerId.clear();

    if (node->type != LayerType::Group) {
        MapDoc* d = doc();
        const QVector<LayerPtr> flat = flatLayers();
        if (d) for (int i = 0; i < flat.size(); ++i) {
            if (!flat[i] || flat[i]->id != primary) continue;
            d->activeLayerIdx = i;
            d->activeLayerId = primary;
            break;
        }
    } else {
        session.selectedObjectId.clear();
        session.selectedObjectIds.clear();
    }
    emit selectionChanged();
}

static LayerPtr findNodeRec(QVector<LayerPtr>& nodes, const QString& id,
                            QVector<LayerPtr>** parentOut, int* indexOut,
                            QSet<quintptr>& visited)
{
    for (int i = 0; i < nodes.size(); ++i) {
        const LayerPtr& n = nodes[i];
        if (!n) continue;
        const quintptr key = quintptr(n.data());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (n->id == id) {
            if (parentOut) *parentOut = &nodes;
            if (indexOut)  *indexOut = i;
            return n;
        }
        if (!n->children.isEmpty()) {
            LayerPtr r = findNodeRec(n->children, id, parentOut, indexOut, visited);
            if (r) return r;
        }
    }
    return LayerPtr();
}

static LayerPtr findNodeRecConst(const QVector<LayerPtr>& nodes, const QString& id,
                                 QSet<quintptr>& visited)
{
    for (const LayerPtr& n : nodes) {
        if (!n) continue;
        const quintptr key = quintptr(n.data());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (n->id == id) return n;
        const LayerPtr r = findNodeRecConst(n->children, id, visited);
        if (r) return r;
    }
    return LayerPtr();
}

LayerPtr Editor::findNode(const QString& id, QVector<LayerPtr>** parentOut, int* indexOut)
{
    if (id.isEmpty()) return LayerPtr();
    QSet<quintptr> visited;
    return findNodeRec(layers(), id, parentOut, indexOut, visited);
}

LayerPtr Editor::findNode(const QString& id) const
{
    if (id.isEmpty()) return LayerPtr();
    QSet<quintptr> visited;
    return findNodeRecConst(layers(), id, visited);
}

LayerPtr Editor::addLayer(LayerPtr layer, const QString& parentGroupId)
{
    MapDoc* d = doc();
    if (!d || !layer) return LayerPtr();
    if (!parentGroupId.isEmpty()) {
        LayerPtr parent = findNode(parentGroupId);
        if (parent && parent->isContainer()) {
            parent->collapsed = false;
            parent->children.push_back(layer);
            setSelectedLayerById(layer->id);
            markDirty();
            emit layersChanged();
            emit mapChanged();
            return layer;
        }
    }
    // UX de grupos: se o próprio Grupo estiver selecionado, uma nova camada
    // entra DENTRO dele. Antes ela virava irmã do grupo, o que fazia a árvore
    // parecer correta visualmente mas quebrava o fluxo Grupo -> Camadas.
    if (const LayerPtr selected = selectedLayer()) {
        if (selected->type == LayerType::Group) {
            selected->collapsed = false;
            selected->children.push_back(layer);
            setSelectedLayerById(layer->id);
            markDirty();
            emit layersChanged();
            emit mapChanged();
            return layer;
        }
    }

    QVector<LayerPtr>* siblings = nullptr;
    int selectedIndex = -1;
    if (const LayerPtr selected = selectedLayer())
        findNode(selected->id, &siblings, &selectedIndex);
    if (siblings && selectedIndex >= 0) siblings->insert(selectedIndex + 1, layer);
    else d->layers.push_back(layer);
    setSelectedLayerById(layer->id);
    markDirty();
    emit layersChanged();
    emit mapChanged();
    return layer;
}

void Editor::removeLayer(const QString& id)
{
    QVector<LayerPtr>* parent = nullptr;
    int index = -1;
    LayerPtr n = findNode(id, &parent, &index);
    if (!n || !parent) return;

    const QString previousActiveId = activeLayer() ? activeLayer()->id : QString();
    const int previousActiveIdx = activeLayerIdx();
    const bool removesActive = !previousActiveId.isEmpty() && layerSubtreeContains(n, previousActiveId);
    const bool removesSelection = !session.selectedLayerId.isEmpty() && layerSubtreeContains(n, session.selectedLayerId);
    const bool removesMaskEdit = !session.selectedMaskLayerId.isEmpty() && layerSubtreeContains(n, session.selectedMaskLayerId);

    parent->remove(index);
    MapDoc* d = doc();
    if (d) {
        const QVector<LayerPtr> flat = flatLayers();
        if (flat.isEmpty()) {
            d->activeLayerIdx = -1;
            d->activeLayerId.clear();
        } else if (!removesActive && !previousActiveId.isEmpty() && findNode(previousActiveId)) {
            for (int i = 0; i < flat.size(); ++i)
                if (flat[i] && flat[i]->id == previousActiveId) {
                    d->activeLayerIdx = i;
                    d->activeLayerId = previousActiveId;
                    break;
                }
        } else {
            d->activeLayerIdx = clampi(previousActiveIdx, 0, flat.size() - 1);
            d->activeLayerId = flat[d->activeLayerIdx] ? flat[d->activeLayerIdx]->id : QString();
        }
    }
    if (removesSelection) session.selectedLayerId = d ? d->activeLayerId : QString();
    if (removesMaskEdit) session.selectedMaskLayerId.clear();
    if (removesActive || removesSelection) {
        // Se a camada de objetos (ou um grupo que a continha) foi removida,
        // nenhum painel/canvas deve continuar segurando IDs de objetos órfãos.
        session.selectedObjectId.clear();
        session.selectedObjectIds.clear();
    }

    // Remoção é uma mudança estrutural do documento. Antes este caminho não
    // marcava dirty nem avisava o painel de camadas, deixando árvore/Inspector
    // visualmente obsoletos até outra ação forçar um refresh.
    markDirty();
    emit layersChanged();
    emit selectionChanged();
    emit mapChanged();
}

void Editor::duplicateLayer(const QString& id)
{
    QVector<LayerPtr>* parent = nullptr;
    int index = -1;
    LayerPtr n = findNode(id, &parent, &index);
    if (!n || !parent) return;
    LayerPtr copy = cloneLayer(n, true);
    copy->name = n->name + QStringLiteral(" (cópia)");
    parent->insert(index + 1, copy);
    setSelectedLayerById(copy->id);
    markDirty();
    emit layersChanged();
    emit mapChanged();
}

bool Editor::mergeDown(const QString& id, QString* error)
{
    QVector<LayerPtr>* parent = nullptr;
    int index = -1;
    LayerPtr top = findNode(id, &parent, &index);
    if (!top || !parent || index <= 0) {
        if (error) *error = QObject::tr("Não há uma camada irmã abaixo para mesclar.");
        return false;
    }
    LayerPtr below = parent->at(index - 1);
    if (!below) {
        if (error) *error = QObject::tr("A camada abaixo não está disponível.");
        return false;
    }

    auto isMergeLeafType = [](const LayerPtr& layer) {
        return layer && (layer->type == LayerType::Tile ||
                         layer->type == LayerType::Object ||
                         layer->type == LayerType::Image);
    };
    auto hasRasterMask = [](const LayerPtr& layer) {
        return layer && (layer->type == LayerType::Tile || layer->type == LayerType::Image) &&
               !layer->imageMask.isNull();
    };
    auto needsBake = [&](const LayerPtr& layer) {
        return layer && (layer->isMask || !layer->children.isEmpty() || hasRasterMask(layer));
    };

    const bool bakeMerge = needsBake(top) || needsBake(below);

    // ------------------------------------------------------------------ Bake
    // Máscaras raster e máscaras estruturais não cabem dentro de uma única
    // Tile/Object Layer sem perder transparência parcial/recorte. Quando uma
    // das duas camadas usa máscara, a mesclagem passa a ser um bake visual:
    // o Renderer compõe exatamente as duas irmãs e o resultado vira uma Paint
    // Layer raster editável. Ctrl+Z continua restaurando a estrutura original.
    if (bakeMerge) {
        if (!isMergeLeafType(top) || !isMergeLeafType(below)) {
            if (error) *error = QObject::tr("O Bake de máscara aceita camadas de Tiles, Objetos ou Imagem.");
            return false;
        }
        if ((top->type == LayerType::Image && top->imageReferenceOnly) ||
            (below->type == LayerType::Image && below->imageReferenceOnly)) {
            if (error) *error = QObject::tr("Imagens de referência não podem entrar no Bake. Converta-as em camada de imagem normal primeiro.");
            return false;
        }
        if ((top->isMask && top->type != LayerType::Tile) ||
            (below->isMask && below->type != LayerType::Tile)) {
            if (error) *error = QObject::tr("O Bake de recorte estrutural está disponível para máscaras de Tiles. Máscaras raster funcionam em Tiles e Imagens.");
            return false;
        }
        if (top->visible != below->visible) {
            if (error) *error = QObject::tr("Para fazer o Bake sem perder uma camada oculta, deixe as duas camadas com a mesma visibilidade.");
            return false;
        }
        if (top->zMode != below->zMode || top->depthLevel != below->depthLevel) {
            if (error) *error = QObject::tr("As duas camadas precisam usar a mesma ordem em relação ao jogador e o mesmo nível de altura antes do Bake.");
            return false;
        }
        if (top->blendMode != QLatin1String("source-over") ||
            below->blendMode != QLatin1String("source-over")) {
            if (error) *error = QObject::tr("Para preservar o resultado do Bake, deixe a Mistura das duas camadas em Normal. Modos como Multiplicar/Tela dependem das camadas que estão atrás.");
            return false;
        }

        MapDoc* d = doc();
        if (!d) {
            if (error) *error = QObject::tr("Nenhum mapa está ativo.");
            return false;
        }
        const QSize mapSize(qMax(1, d->map.pixelWidth()), qMax(1, d->map.pixelHeight()));
        QImage composed(mapSize, QImage::Format_ARGB32_Premultiplied);
        composed.fill(Qt::transparent);

        // Se ambas estão ocultas, ainda precisamos assar o conteúdo para que o
        // resultado volte correto quando a nova camada for mostrada depois.
        LayerPtr belowCopy = cloneLayer(below, false);
        LayerPtr topCopy = cloneLayer(top, false);
        belowCopy->visible = true;
        topCopy->visible = true;
        QVector<LayerPtr> bakeNodes{belowCopy, topCopy};

        RenderOptions opt;
        opt.fillBackground = false;
        opt.skipReferenceLayers = true;
        opt.drawObjectFrames = false;
        opt.highlightActive = false;
        opt.cacheMaskPaths = false;
        opt.animationTimeMs = 0;
        {
            QPainter painter(&composed);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.setClipRect(QRect(QPoint(0, 0), mapSize));
            drawLayerTree(painter, *this, bakeNodes, 1.0, opt);
        }

        int minX = composed.width(), minY = composed.height(), maxX = -1, maxY = -1;
        for (int y = 0; y < composed.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(composed.constScanLine(y));
            for (int x = 0; x < composed.width(); ++x) {
                if (qAlpha(line[x]) == 0) continue;
                minX = qMin(minX, x); minY = qMin(minY, y);
                maxX = qMax(maxX, x); maxY = qMax(maxY, y);
            }
        }
        QRect bounds;
        if (maxX >= minX && maxY >= minY)
            bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        else
            bounds = QRect(0, 0, 1, 1);

        QImage bakedPixels = composed.copy(bounds);
        LayerPtr baked = makePaintLayer(bakedPixels.size(),
            QObject::tr("%1 + %2 (Bake)").arg(below->name, top->name));
        baked->image = bakedPixels;
        baked->imagewidth = bakedPixels.width();
        baked->imageheight = bakedPixels.height();
        baked->offsetx = bounds.x();
        baked->offsety = bounds.y();
        baked->visible = below->visible;
        baked->locked = below->locked || top->locked;
        baked->opacity = 1.0;
        baked->blendMode = QStringLiteral("source-over");
        baked->zMode = below->zMode;
        baked->depthLevel = below->depthLevel;
        baked->uiColor = top->uiColor.isValid() ? top->uiColor : below->uiColor;
        baked->imageFilter = QStringLiteral("nearest");
        baked->imagePaintLayer = true;

        const bool removesMaskEdit = !session.selectedMaskLayerId.isEmpty() &&
            (layerSubtreeContains(top, session.selectedMaskLayerId) ||
             layerSubtreeContains(below, session.selectedMaskLayerId));

        (*parent)[index - 1] = baked;
        parent->removeAt(index);
        if (removesMaskEdit) session.selectedMaskLayerId.clear();
        setSelectedLayerById(baked->id);
        setActiveLayerById(baked->id);
        session.selectedObjectId.clear();
        session.selectedObjectIds.clear();
        invalidateMaskPathCache();
        markDirty();
        emit layersChanged();
        emit selectionChanged();
        emit mapChanged();
        return true;
    }

    // ---------------------------------------------------------- merge editável
    // Caminho antigo: mantém Tile/Object editável quando não há máscara para
    // assar. O resultado continua sendo a mesma estrutura de dados, sem raster.
    if (top->type != below->type ||
        (top->type != LayerType::Tile && top->type != LayerType::Object)) {
        if (error) *error = QObject::tr("A mesclagem editável exige duas camadas irmãs do mesmo tipo (Tile ou Object). Para máscaras, o editor usa Bake automaticamente.");
        return false;
    }
    if (top->isMask || below->isMask || !top->children.isEmpty() || !below->children.isEmpty()) {
        if (error) *error = QObject::tr("Esta estrutura precisa ser mesclada por Bake.");
        return false;
    }
    const bool visualCompatible = top->visible == below->visible &&
        qFuzzyCompare(top->opacity + 1.0, below->opacity + 1.0) &&
        top->blendMode == below->blendMode && top->zMode == below->zMode &&
        top->depthLevel == below->depthLevel;
    if (!visualCompatible) {
        if (error) *error = QObject::tr(
            "As duas camadas usam visibilidade/opacidade/mistura/ordem Z/nível de altura diferentes. Igualize essas propriedades antes de mesclar para preservar o visual.");
        return false;
    }

    if (top->type == LayerType::Tile) {
        if (top->tileWidth != below->tileWidth || top->tileHeight != below->tileHeight ||
            top->offsetx != below->offsetx || top->offsety != below->offsety ||
            top->cols != below->cols || top->rows != below->rows) {
            if (error) *error = QObject::tr(
                "As duas Tile Layers precisam usar a mesma grade, tamanho e posição para serem mescladas sem deslocar tiles.");
            return false;
        }
        for (int y = 0; y < top->rows; ++y) {
            for (int x = 0; x < top->cols; ++x) {
                const Cell& src = top->data2D[y][x];
                if (src.isEmpty()) continue;
                Cell dst = below->data2D[y][x];
                dst += src;
                below->data2D[y][x] = dst;
            }
        }
    } else {
        const double dx = top->offsetx - below->offsetx;
        const double dy = top->offsety - below->offsety;
        for (MapObject object : std::as_const(top->objects)) {
            object.x += dx;
            object.y += dy;
            below->objects.push_back(object);
        }
    }

    parent->removeAt(index);
    below->locked = below->locked || top->locked;
    setSelectedLayerById(below->id);
    setActiveLayerById(below->id);
    session.selectedObjectId.clear();
    session.selectedObjectIds.clear();
    markDirty();
    emit layersChanged();
    emit selectionChanged();
    emit mapChanged();
    return true;
}

namespace {
QRect opaqueBounds(const QImage& image)
{
    if (image.isNull()) return QRect();
    int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 0) continue;
            minX = qMin(minX, x); minY = qMin(minY, y);
            maxX = qMax(maxX, x); maxY = qMax(maxY, y);
        }
    }
    return maxX < minX || maxY < minY
        ? QRect()
        : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

bool imageRectHasAlpha(const QImage& image, const QRect& rect)
{
    if (image.isNull()) return false;
    const QRect clipped = rect.intersected(image.rect());
    if (clipped.isEmpty()) return false;
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = clipped.left(); x <= clipped.right(); ++x)
            if (qAlpha(line[x]) != 0) return true;
    }
    return false;
}

QString uniqueBakeTilesetName(const Editor& ed, const QString& layerName)
{
    const QString root = QObject::tr("Bake — %1").arg(layerName.trimmed().isEmpty()
        ? QObject::tr("Camada") : layerName.trimmed());
    auto exists = [&](const QString& candidate) {
        for (const Tileset& ts : ed.tilesets)
            if (ts.name.compare(candidate, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    if (!exists(root)) return root;
    for (int n = 2; n < 10000; ++n) {
        const QString candidate = QObject::tr("%1 (%2)").arg(root).arg(n);
        if (!exists(candidate)) return candidate;
    }
    return root + QStringLiteral(" ") + idGen().left(6);
}
}

bool Editor::bakeLayerToTileset(const QString& id, bool showInPalette, QString* error)
{
    QVector<LayerPtr>* parent = nullptr;
    int index = -1;
    LayerPtr source = findNode(id, &parent, &index);
    MapDoc* d = doc();
    if (!source || !parent || !d) {
        if (error) *error = QObject::tr("Selecione uma camada para fazer o Bake.");
        return false;
    }
    if (source->type == LayerType::Image && source->imageReferenceOnly) {
        if (error) *error = QObject::tr("Imagens usadas apenas como referência não entram no Bake. Ative a exportação da imagem primeiro.");
        return false;
    }
    if (source->blendMode != QLatin1String("source-over")) {
        if (error) *error = QObject::tr("Para preservar o resultado visual, deixe 'Como mistura com as camadas abaixo' em Normal antes do Bake.");
        return false;
    }

    // Documento + biblioteca global mudam juntos; o histórico precisa tratar
    // os dois lados como uma única operação atômica.
    const DocSnapshot beforeDoc = snapshotDoc();
    const QVector<Tileset> beforeTilesets = tilesets;
    const int beforeActiveTilesetIdx = session.activeTilesetIdx;
    const TilesetSelection beforeTsSel = session.tsSel;
    const CustomStamp beforeCustomStamp = session.customStamp;

    const int tw = qMax(1, d->map.tileWidth);
    const int th = qMax(1, d->map.tileHeight);
    const QSize mapSize(qMax(1, d->map.pixelWidth()), qMax(1, d->map.pixelHeight()));
    QImage composed(mapSize, QImage::Format_ARGB32_Premultiplied);
    composed.fill(Qt::transparent);

    // Renderiza a subárvore escolhida exatamente como o compositor do editor:
    // máscaras, grupos, filtros, sombra de contato, repetição de Image Layer,
    // opacidade e transformações já chegam assados ao Tileset final.
    LayerPtr rasterSource = cloneLayer(source, false);
    rasterSource->visible = true;
    {
        QPainter painter(&composed);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setClipRect(QRect(QPoint(0, 0), mapSize));
        RenderOptions options;
        options.fillBackground = false;
        options.skipReferenceLayers = false;
        options.drawObjectFrames = false;
        options.highlightActive = false;
        options.cacheMaskPaths = false;
        options.animationTimeMs = 0;
        drawLayerTree(painter, *this, QVector<LayerPtr>{rasterSource}, 1.0, options);
    }

    const QRect localOpaque = opaqueBounds(composed);
    if (localOpaque.isEmpty()) {
        if (error) *error = QObject::tr("A camada não produziu pixels visíveis dentro do mapa.");
        return false;
    }

    // O recorte sempre fecha em células completas da grade-base. Assim o
    // resultado pode voltar para uma Tile Layer sem deslocamento subpixel.
    const int startCellX = qBound(0, localOpaque.left() / tw, d->map.width - 1);
    const int startCellY = qBound(0, localOpaque.top() / th, d->map.height - 1);
    const int endCellX = qBound(startCellX + 1, localOpaque.right() / tw + 1, d->map.width);
    const int endCellY = qBound(startCellY + 1, localOpaque.bottom() / th + 1, d->map.height);
    const int atlasCols = qMax(1, endCellX - startCellX);
    const int atlasRows = qMax(1, endCellY - startCellY);
    const QRect atlasRect(startCellX * tw, startCellY * th, atlasCols * tw, atlasRows * th);
    const QImage atlasImage = composed.copy(atlasRect).convertToFormat(QImage::Format_ARGB32_Premultiplied);

    Tileset generated = makeTileset(atlasImage, uniqueBakeTilesetName(*this, source->name), tw, th, 0, 0);
    generated.category = QStringLiteral("Convertidos");
    generated.generatedFromBake = true;
    generated.bakeSourceLayerId = source->id;
    generated.paletteVisible = showInPalette;

    constexpr int kBakeTextureLimit = 4096;
    QVector<Tileset> parts = splitTilesetForTextureLimit(generated, kBakeTextureLimit);
    if (parts.isEmpty()) parts.push_back(generated);

    const int firstGeneratedIndex = tilesets.size();
    for (Tileset& part : parts) {
        part.generatedFromBake = true;
        part.bakeSourceLayerId = source->id;
        part.paletteVisible = showInPalette;
        tilesets.push_back(part);
    }
    reindexTilesetGids();

    LayerPtr baked = makeTileLayer(source->name, tw, th, d->map.width, d->map.height);
    baked->id = source->id;
    baked->visible = source->visible;
    baked->locked = source->locked;
    // Opacidade/mistura foram rasterizadas; reaplicá-las produziria resultado
    // diferente. Ordem visual e nível de altura continuam propriedades da camada.
    baked->opacity = 1.0;
    baked->blendMode = QStringLiteral("source-over");
    baked->zMode = source->zMode;
    baked->depthLevel = source->depthLevel;
    baked->uiColor = source->uiColor;
    baked->collapsed = source->collapsed;

    CustomStamp bakedStamp;
    bakedStamp.w = atlasCols;
    bakedStamp.h = atlasRows;
    TileRef firstRef;
    auto refForAtlasCell = [&](int ax, int ay) -> TileRef {
        for (int partNo = 0; partNo < parts.size(); ++partNo) {
            const Tileset& part = parts[partNo];
            const int ox = part.sourceTileX - generated.sourceTileX;
            const int oy = part.sourceTileY - generated.sourceTileY;
            if (ax < ox || ay < oy || ax >= ox + part.columns || ay >= oy + part.rows) continue;
            TileRef ref;
            ref.tilesetIdx = firstGeneratedIndex + partNo;
            ref.tx = ax - ox;
            ref.ty = ay - oy;
            return ref;
        }
        return TileRef();
    };

    for (int ay = 0; ay < atlasRows; ++ay) {
        for (int ax = 0; ax < atlasCols; ++ax) {
            const QRect tilePixels(ax * tw, ay * th, tw, th);
            if (!imageRectHasAlpha(atlasImage, tilePixels)) continue;
            const TileRef ref = refForAtlasCell(ax, ay);
            if (!ref.isValid()) continue;
            const int gx = startCellX + ax, gy = startCellY + ay;
            if (baked->inBounds(gx, gy)) baked->setCell(gx, gy, Cell{ref});
            bakedStamp.tiles.push_back(ref);
            bakedStamp.offsets.push_back(QPoint(ax, ay));
            if (!firstRef.isValid()) firstRef = ref;
        }
    }

    if (bakedStamp.tiles.isEmpty()) {
        tilesets = beforeTilesets;
        reindexTilesetGids();
        if (error) *error = QObject::tr("O Bake não conseguiu gerar tiles visíveis.");
        return false;
    }

    const bool removesMaskEdit = !session.selectedMaskLayerId.isEmpty() &&
        layerSubtreeContains(source, session.selectedMaskLayerId);
    (*parent)[index] = baked;
    if (removesMaskEdit) session.selectedMaskLayerId.clear();
    setSelectedLayerById(baked->id);
    setActiveLayerById(baked->id);
    session.selectedObjectId.clear();
    session.selectedObjectIds.clear();

    if (showInPalette) {
        session.customStamp = bakedStamp;
        session.activeTilesetIdx = firstRef.tilesetIdx;
        session.tsSel = TilesetSelection{firstRef.tilesetIdx, firstRef.tx, firstRef.ty, 1, 1};
    } else {
        // Recurso auxiliar continua no projeto e sustenta a Tile Layer, mas não
        // sequestra a paleta nem a seleção que o autor estava usando.
        session.activeTilesetIdx = beforeActiveTilesetIdx;
        session.tsSel = beforeTsSel;
        session.customStamp = beforeCustomStamp;
    }

    HistoryEntry history;
    history.document = true;
    history.tilesetSnapshot = true;
    history.label = QObject::tr("Bake de camada para Tileset");
    history.beforeDoc = beforeDoc;
    history.afterDoc = snapshotDoc();
    history.beforeTilesets = beforeTilesets;
    history.afterTilesets = tilesets;
    history.beforeActiveTilesetIdx = beforeActiveTilesetIdx;
    history.afterActiveTilesetIdx = session.activeTilesetIdx;
    history.beforeTsSel = beforeTsSel;
    history.afterTsSel = session.tsSel;
    history.beforeCustomStamp = beforeCustomStamp;
    history.afterCustomStamp = session.customStamp;
    d->history.resize(d->historyPtr + 1);
    d->history.push_back(std::move(history));
    d->historyPtr = d->history.size() - 1;
    pruneHistory(d);
    ++d->historyRevision;

    invalidateMaskPathCache();
    markDirty();
    emit tilesetsChanged();
    emit layersChanged();
    emit selectionChanged();
    emit historyChanged();
    emit mapChanged();
    return true;
}

bool Editor::bakeObjectLayerToTileset(const QString& id, QString* error)
{
    const LayerPtr source = findNode(id);
    if (!source || source->type != LayerType::Object) {
        if (error) *error = QObject::tr("Selecione uma Camada de objetos para fazer o Bake.");
        return false;
    }
    return bakeLayerToTileset(id, true, error);
}

namespace {
void shiftLayerContentRecursive(const LayerPtr& layer, int dxPx, int dyPx)
{
    if (!layer) return;
    if (layer->type == LayerType::Tile) {
        const int tw = qMax(1, layer->tileWidth), th = qMax(1, layer->tileHeight);
        if (dxPx % tw == 0 && dyPx % th == 0) {
            const int dx = dxPx / tw, dy = dyPx / th;
            QVector<QVector<Cell>> shifted(layer->rows, QVector<Cell>(layer->cols));
            for (int y = 0; y < layer->rows; ++y)
                for (int x = 0; x < layer->cols; ++x) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < layer->cols && ny < layer->rows)
                        shifted[ny][nx] = layer->data2D[y][x];
                }
            layer->data2D = std::move(shifted);
        } else {
            // Grades secundárias que não dividem exatamente a grade-base são
            // deslocadas por offset para preservar a posição visual exata.
            layer->offsetx += dxPx;
            layer->offsety += dyPx;
        }
    } else if (layer->type == LayerType::Object) {
        for (MapObject& object : layer->objects) {
            object.x += dxPx;
            object.y += dyPx;
        }
    } else if (layer->type == LayerType::Image) {
        layer->offsetx += dxPx;
        layer->offsety += dyPx;
    }
    for (const LayerPtr& child : layer->children)
        shiftLayerContentRecursive(child, dxPx, dyPx);
}
} // namespace

bool Editor::shiftMapContents(int dxTiles, int dyTiles, bool shiftRegions, QString* error)
{
    MapDoc* d = doc();
    if (!d) {
        if (error) *error = QObject::tr("Nenhum mapa ativo.");
        return false;
    }
    if (dxTiles == 0 && dyTiles == 0) return true;
    const int dxPx = dxTiles * d->map.tileWidth;
    const int dyPx = dyTiles * d->map.tileHeight;
    for (const LayerPtr& layer : d->layers)
        shiftLayerContentRecursive(layer, dxPx, dyPx);

    if (shiftRegions) {
        QHash<quint64, quint8> shifted;
        for (auto it = d->rpgMakerRegions.cbegin(); it != d->rpgMakerRegions.cend(); ++it) {
            const int nx = MapDoc::regionX(it.key()) + dxTiles;
            const int ny = MapDoc::regionY(it.key()) + dyTiles;
            if (nx >= 0 && ny >= 0 && nx < d->map.width && ny < d->map.height && it.value() > 0)
                shifted.insert(MapDoc::regionKey(nx, ny), it.value());
        }
        d->rpgMakerRegions = std::move(shifted);
    }

    session.selectedObjectId.clear();
    session.selectedObjectIds.clear();
    markDirty();
    emit layersChanged();
    emit selectionChanged();
    emit mapChanged();
    return true;
}

void Editor::moveLayer(const QString& id, int delta)
{
    QVector<LayerPtr>* parent = nullptr;
    int index = -1;
    LayerPtr n = findNode(id, &parent, &index);
    if (!n || !parent) return;
    const int target = index + delta;
    if (target < 0 || target >= parent->size()) return;
    parent->swapItemsAt(index, target);
    setSelectedLayerById(id);
    markDirty();
    emit layersChanged();
    emit mapChanged();
}

bool Editor::reparentLayer(const QString& id, const QString& newParentId, int index)
{
    QVector<LayerPtr>* parent = nullptr;
    int oldIndex = -1;
    LayerPtr node = findNode(id, &parent, &oldIndex);
    if (!node || !parent) return false;
    if (id == newParentId) return false;
    if (!newParentId.isEmpty() && layerSubtreeContains(node, newParentId)) return false; // ciclo

    QVector<LayerPtr>* target = nullptr;
    if (newParentId.isEmpty()) {
        target = &layers();
    } else {
        LayerPtr p = findNode(newParentId);
        if (!p || !p->isContainer()) return false;
        target = &p->children;
    }
    parent->remove(oldIndex);
    if (target == parent && index > oldIndex) --index;
    index = clampi(index, 0, target->size());
    target->insert(index, node);
    setSelectedLayerById(id);
    markDirty();
    emit layersChanged();
    emit mapChanged();
    return true;
}

bool Editor::applyLayerTreeOrder(const QVector<LayerTreePlacement>& placements, QString* error)
{
    MapDoc* d = doc();
    if (!d) {
        if (error) *error = QStringLiteral("Nenhum mapa ativo para reorganizar camadas.");
        return false;
    }
    const QString activeId = activeLayer() ? activeLayer()->id : QString();
    if (!applyLayerTreePlacements(d->layers, placements, error)) return false;

    const QVector<LayerPtr> flat = flatLayers();
    d->activeLayerIdx = -1;
    d->activeLayerId.clear();
    if (!flat.isEmpty()) {
        int target = 0;
        if (!activeId.isEmpty())
            for (int i = 0; i < flat.size(); ++i)
                if (flat[i] && flat[i]->id == activeId) { target = i; break; }
        d->activeLayerIdx = target;
        d->activeLayerId = flat[target] ? flat[target]->id : QString();
    }
    if (!session.selectedLayerId.isEmpty() && !findNode(session.selectedLayerId))
        session.selectedLayerId = d->activeLayerId;

    markDirty();
    emit layersChanged();
    emit selectionChanged();
    emit mapChanged();
    return true;
}

static void resyncRec(QVector<LayerPtr>& nodes, const MapInfo& info, QSet<quintptr>& visited)
{
    for (const LayerPtr& n : nodes) {
        if (!n) continue;
        const quintptr key = quintptr(n.data());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (n->type == LayerType::Tile) {
            const int cols = qMax(1, (info.pixelWidth()  + n->tileWidth  - 1) / n->tileWidth);
            const int rows = qMax(1, (info.pixelHeight() + n->tileHeight - 1) / n->tileHeight);
            if (cols != n->cols || rows != n->rows) n->resizeGrid(cols, rows);
            if (!n->imageMask.isNull()) {
                const QSize target(qMax(1, n->cols * n->tileWidth), qMax(1, n->rows * n->tileHeight));
                if (n->imageMask.size() != target) {
                    QImage resizedMask(target, QImage::Format_ARGB32_Premultiplied);
                    resizedMask.fill(Qt::white);
                    QPainter painter(&resizedMask);
                    painter.drawImage(QPoint(0, 0), n->imageMask);
                    painter.end();
                    n->imageMask = resizedMask;
                }
            }
        } else if (n->type == LayerType::Image && n->imagePaintLayer) {
            const QSize target(qMax(1, info.pixelWidth()), qMax(1, info.pixelHeight()));
            if (n->image.size() != target) {
                QImage resized(target, QImage::Format_ARGB32_Premultiplied);
                resized.fill(Qt::transparent);
                QPainter painter(&resized);
                painter.drawImage(QPoint(0,0), n->image);
                painter.end();
                n->image = resized;
                n->imagewidth = resized.width();
                n->imageheight = resized.height();
                if (!n->imageMask.isNull()) {
                    QImage resizedMask(target, QImage::Format_ARGB32_Premultiplied);
                    resizedMask.fill(Qt::transparent);
                    QPainter maskPainter(&resizedMask);
                    maskPainter.drawImage(QPoint(0, 0), n->imageMask);
                    maskPainter.end();
                    n->imageMask = resizedMask;
                }
                n->offsetx = 0; n->offsety = 0;
                n->imageScaleX = n->imageScaleY = 1.0;
                n->imageRotation = 0.0;
                n->imageFlipX = n->imageFlipY = false;
            }
        }
        if (!n->children.isEmpty()) resyncRec(n->children, info, visited);
    }
}

void Editor::resyncLayerGrids()
{
    QSet<quintptr> visited;
    resyncRec(layers(), mapInfo(), visited);
    if (MapDoc* d = doc()) d->pruneRegions();
    // Tilesets/Autotiles 2.0: redimensionar muda quais celulas sao borda.
    // Reaplica somente as variantes de contorno; o interior permanece intacto.
    rebuildAllAutotileTopologies(*this, false);
    emit mapChanged();
}

// ----------------------------------------------------------------- tilesets
int Editor::nextFirstGid() const
{
    int g = 1;
    for (const Tileset& ts : tilesets) g += ts.tilecount;
    return g;
}

int Editor::addTileset(const Tileset& ts)
{
    Tileset copy = ts;
    copy.firstgid = nextFirstGid();
    tilesets.push_back(copy);
    // Qualquer Tileset que chegue com AnimatedAutotile/Wang legado recebe
    // imediatamente uma identidade semantica 2.0; a UI nunca precisa esperar
    // salvar/reabrir para enxergar o catalogo de Autotiles.
    reconcileTilesetAutotiles(*this);
    session.activeTilesetIdx = tilesets.size() - 1;
    session.tsSel = TilesetSelection{ session.activeTilesetIdx, 0, 0, 1, 1 };
    reindexTilesetGids();
    markDirty();
    emit tilesetsChanged();
    emit selectionChanged();
    return session.activeTilesetIdx;
}

/// Remove o tileset e remapeia todas as referencias (celulas, objetos, eventos,
/// wang, marcadores e pool) — nenhuma referencia por indice pode apontar para
/// o tileset seguinte depois que a lista encolhe.
static void remapCellsRec(QVector<LayerPtr>& nodes, int removed)
{
    for (const LayerPtr& n : nodes) {
        if (!n) continue;
        if (n->type == LayerType::Tile) {
            for (QVector<Cell>& row : n->data2D)
                for (Cell& c : row) {
                    Cell out;
                    for (const TileRef& t : c) {
                        if (t.tilesetIdx == removed) continue;
                        TileRef nt = t;
                        if (nt.tilesetIdx > removed) --nt.tilesetIdx;
                        out.push_back(nt);
                    }
                    c = out;
                }
        } else if (n->type == LayerType::Object) {
            for (MapObject& o : n->objects) {
                QVector<TileRef> out;
                for (const TileRef& t : o.tiles) {
                    if (t.tilesetIdx == removed) continue;
                    TileRef nt = t;
                    if (nt.tilesetIdx > removed) --nt.tilesetIdx;
                    out.push_back(nt);
                }
                o.tiles = out;
            }
        }
        if (!n->children.isEmpty()) remapCellsRec(n->children, removed);
    }
}

static QHash<QString, bool> remapKeyMap(const QHash<QString, bool>& src, int removed)
{
    QHash<QString, bool> out;
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() != 3) continue;
        int ts = parts[0].toInt();
        if (ts == removed) continue;
        if (ts > removed) --ts;
        out.insert(tileKey(ts, parts[1].toInt(), parts[2].toInt()), it.value());
    }
    return out;
}

void Editor::removeTileset(int idx)
{
    if (idx < 0 || idx >= tilesets.size()) return;
    for (MapDoc& d : docs)
        remapCellsRec(d.layers, idx);

    starTiles = remapKeyMap(starTiles, idx);

    QVector<RandomEntry> pool;
    for (RandomEntry e : randomPool) {
        if (e.tilesetIdx == idx) continue;
        if (e.tilesetIdx > idx) --e.tilesetIdx;
        for (TileRef& t : e.tiles) if (t.tilesetIdx > idx) --t.tilesetIdx;
        pool.push_back(e);
    }
    randomPool = pool;

    QVector<SavedStamp> patterns;
    for (SavedStamp saved : savedStamps) {
        QVector<TileRef> tiles;
        QVector<QPoint> offsets;
        for (int i = 0; i < saved.stamp.tiles.size() && i < saved.stamp.offsets.size(); ++i) {
            TileRef t = saved.stamp.tiles[i];
            if (t.tilesetIdx == idx) continue;
            if (t.tilesetIdx > idx) --t.tilesetIdx;
            tiles.push_back(t); offsets.push_back(saved.stamp.offsets[i]);
        }
        saved.stamp.tiles = tiles; saved.stamp.offsets = offsets;
        if (saved.stamp.valid()) patterns.push_back(saved);
    }
    savedStamps = patterns;

    QVector<TileRef> recent;
    for (TileRef t : recentTiles) {
        if (t.tilesetIdx == idx) continue;
        if (t.tilesetIdx > idx) --t.tilesetIdx;
        recent.push_back(t);
    }
    recentTiles = recent;

    for (WangSet& ws : wangSets) {
        QHash<QString, WangTileData> tiles;
        for (auto it = ws.tiles.constBegin(); it != ws.tiles.constEnd(); ++it) {
            int ts, tx, ty;
            if (!WangSet::parseTileKey(it.key(), &ts, &tx, &ty)) continue;
            if (ts == idx) continue;
            if (ts > idx) --ts;
            tiles.insert(WangSet::tileKeyOf(ts, tx, ty), it.value());
        }
        ws.tiles = tiles;
        if (ws.iconTilesetIdx == idx) { ws.hasIcon = false; ws.iconTilesetIdx = -1; }
        else if (ws.iconTilesetIdx > idx) --ws.iconTilesetIdx;
        for (WangColor& c : ws.colors) {
            if (c.iconTilesetIdx == idx) { c.hasIcon = false; c.iconTilesetIdx = -1; }
            else if (c.iconTilesetIdx > idx) --c.iconTilesetIdx;
        }
    }

    const bool removedActiveTileset = session.activeTilesetIdx == idx;
    const QString removedTilesetId = tilesets.at(idx).id;
    for (int i = autotiles.size() - 1; i >= 0; --i)
        if (autotiles.at(i).tilesetId == removedTilesetId) autotiles.removeAt(i);
    tilesets.remove(idx);
    reindexTilesetGids();
    if (removedActiveTileset) {
        const QVector<int> visible = visibleTilesetIndices(*this);
        session.activeTilesetIdx = visible.isEmpty() ? -1 : visible.at(qMin(idx, visible.size() - 1));
        session.activeAutotileId.clear();
        if (session.tool == Tool::Terrain) session.tool = Tool::Stamp;
        session.wangBrushActive = false;
    } else if (session.activeTilesetIdx > idx) {
        --session.activeTilesetIdx;
    } else if (session.activeTilesetIdx >= tilesets.size()) {
        session.activeTilesetIdx = tilesets.size() - 1;
    }
    if (session.tsSel.tilesetIdx == idx) session.tsSel = TilesetSelection();
    else if (session.tsSel.tilesetIdx > idx) --session.tsSel.tilesetIdx;
    session.customStamp.clear();
    markDirty();
    emit tilesetsChanged();
    emit mapChanged();
    emit wangChanged();
    emit patternsChanged();
    emit selectionChanged();
}

void Editor::reindexTilesetGids()
{
    int g = 1;
    for (Tileset& ts : tilesets) { ts.firstgid = g; g += ts.tilecount; }
}

const Tileset* Editor::tilesetAt(int idx) const
{
    if (idx < 0 || idx >= tilesets.size()) return nullptr;
    return &tilesets[idx];
}
Tileset* Editor::tilesetAt(int idx)
{
    if (idx < 0 || idx >= tilesets.size()) return nullptr;
    return &tilesets[idx];
}

int Editor::gidFor(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts) return 0;
    return ts->firstgid + ty * ts->columns + tx;
}

int Editor::tilesetIdxForGid(int gid) const
{
    if (gid <= 0) return -1;
    for (int i = tilesets.size() - 1; i >= 0; --i)
        if (gid >= tilesets[i].firstgid) return i;
    return -1;
}

// -------------------------------------------------------------- marcadores
int Editor::tilePriority(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return 0;
    // Autotiles animados guardam a prioridade no frame/base canônico. Os
    // quadros auxiliares herdam o mesmo valor sem duplicar metadados.
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    return ts->tilePriority(canonical.x(), canonical.y());
}

void Editor::setTilePriority(int tilesetIdx, int tx, int ty, int priority)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    const int normalized = clampi(priority, 0, 5);
    if (ts->tilePriority(canonical.x(), canonical.y()) == normalized) return;
    ts->setTilePriority(canonical.x(), canonical.y(), normalized);

    // `starTiles` fica somente como espelho de compatibilidade para código e
    // projetos antigos. A fonte de verdade agora é Tileset::tilePriorities.
    const QString k = tileKey(tilesetIdx, canonical.x(), canonical.y());
    if (normalized > 0) starTiles.insert(k, true);
    else starTiles.remove(k);

    markDirty();
    emit tilesetsChanged();
    emit mapChanged();
}

void Editor::cycleTilePriority(int tilesetIdx, int tx, int ty)
{
    setTilePriority(tilesetIdx, tx, ty, (tilePriority(tilesetIdx, tx, ty) + 1) % 6);
}

int Editor::applyTilePrioritiesBottomUp(int tilesetIdx, const QVector<QPoint>& tiles)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || tiles.isEmpty()) return 0;

    // Canonicaliza primeiro para que frames auxiliares de autotiles animados
    // nao recebam metadados duplicados nem contem como linhas extras.
    QSet<QPoint> canonicalTiles;
    QSet<int> rowSet;
    for (const QPoint& pt : tiles) {
        if (!ts->contains(pt.x(), pt.y())) continue;
        const QPoint canonical = canonicalAnimatedTile(*ts, pt.x(), pt.y());
        if (!ts->contains(canonical.x(), canonical.y())) continue;
        canonicalTiles.insert(canonical);
        rowSet.insert(canonical.y());
    }
    if (canonicalTiles.isEmpty()) return 0;

    QList<int> rows = rowSet.values();
    std::sort(rows.begin(), rows.end(), [](int a, int b) { return a > b; });
    QHash<int, int> priorityByRow;
    for (int i = 0; i < rows.size(); ++i)
        priorityByRow.insert(rows.at(i), qMin(5, i + 1));

    bool changed = false;
    for (const QPoint& pt : canonicalTiles) {
        const int priority = priorityByRow.value(pt.y(), 1);
        if (ts->tilePriority(pt.x(), pt.y()) == priority) continue;
        ts->setTilePriority(pt.x(), pt.y(), priority);
        const QString k = tileKey(tilesetIdx, pt.x(), pt.y());
        starTiles.insert(k, true);
        changed = true;
    }

    if (changed) {
        markDirty();
        emit tilesetsChanged();
        emit mapChanged();
    }
    return rows.size();
}

bool Editor::isStarMarked(int tilesetIdx, int tx, int ty) const
{
    return tilePriority(tilesetIdx, tx, ty) > 0;
}

void Editor::toggleStar(int tilesetIdx, int tx, int ty)
{
    setTilePriority(tilesetIdx, tx, ty, isStarMarked(tilesetIdx, tx, ty) ? 0 : 1);
}
int Editor::oppositeSide(int side)
{
    switch (side) {
    case SideTop:    return SideBottom;
    case SideBottom: return SideTop;
    case SideLeft:   return SideRight;
    case SideRight:  return SideLeft;
    }
    return 0;
}

bool Editor::isCollisionMarked(int tilesetIdx, int tx, int ty) const
{
    return collisionMask(tilesetIdx, tx, ty) != 0;
}

bool Editor::isTileFullyBlocked(int tilesetIdx, int tx, int ty) const
{
    return collisionMask(tilesetIdx, tx, ty) == int(SideAll);
}

int Editor::collisionMask(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return 0;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    return ts->tileCollisionMask(canonical.x(), canonical.y());
}

void Editor::setCollisionMask(int tilesetIdx, int tx, int ty, int mask)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    const int normalized = mask & SideAll;
    if (ts->tileCollisionMask(canonical.x(), canonical.y()) == normalized) return;
    ts->setTileCollisionMask(canonical.x(), canonical.y(), normalized);
    markDirty();
    emit tilesetsChanged();
    emit mapChanged();
}

void Editor::setTileBlocked(int tilesetIdx, int tx, int ty, bool blocked)
{
    setCollisionMask(tilesetIdx, tx, ty, blocked ? int(SideAll) : 0);
}

void Editor::toggleTileBlocked(int tilesetIdx, int tx, int ty)
{
    // Mascara parcial significa "configuracao detalhada". No Editor
    // principal, o primeiro clique promove para bloqueio inteiro; somente um
    // tile ja totalmente bloqueado volta para Livre. Assim a ferramenta
    // simples nunca cria nem edita lados individualmente.
    setTileBlocked(tilesetIdx, tx, ty, !isTileFullyBlocked(tilesetIdx, tx, ty));
}

void Editor::toggleCollision(int tilesetIdx, int tx, int ty)
{
    toggleTileBlocked(tilesetIdx, tx, ty);
}
double Editor::tileProb(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return 1.0;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    return ts->tileProbability(canonical.x(), canonical.y());
}
void Editor::setTileProb(int tilesetIdx, int tx, int ty, double v)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    if (qFuzzyCompare(ts->tileProbability(canonical.x(), canonical.y()), v)) return;
    ts->setTileProbability(canonical.x(), canonical.y(), v);
    markDirty();
    emit tilesetsChanged();
}

QString Editor::tileReflectionPreset(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return QString();
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    return ts->tileEffectPreset(canonical.x(), canonical.y(), QStringLiteral("reflection"));
}

void Editor::setTileReflectionPreset(int tilesetIdx, int tx, int ty, const QString& preset)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    if (ts->tileEffectPreset(canonical.x(), canonical.y(), QStringLiteral("reflection")) == preset.trimmed()) return;
    ts->setTileEffectPreset(canonical.x(), canonical.y(), QStringLiteral("reflection"), preset);
    markDirty();
    emit tilesetsChanged();
    emit mapChanged();
}

int Editor::tileReflectionOpacityPercent(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return -1;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    const QVector<TileResourceEffectBinding> effects =
        ts->tileResourceEffects.value(Tileset::priorityKey(canonical.x(), canonical.y()));
    return resourceEffectOpacityPercent(effects, QStringLiteral("reflection"));
}

void Editor::setTileReflectionOpacityPercent(int tilesetIdx, int tx, int ty, int percent)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    const QString key = Tileset::priorityKey(canonical.x(), canonical.y());
    QVector<TileResourceEffectBinding> effects = ts->tileResourceEffects.value(key);
    if (resourceEffectPreset(effects, QStringLiteral("reflection")).isEmpty()) return;
    const int normalized = percent < 0 ? -1 : clampi(percent, 0, 100);
    if (resourceEffectOpacityPercent(effects, QStringLiteral("reflection")) == normalized) return;
    setResourceEffectOpacityPercent(effects, QStringLiteral("reflection"), normalized);
    if (effects.isEmpty()) ts->tileResourceEffects.remove(key);
    else ts->tileResourceEffects.insert(key, effects);
    markDirty();
    emit tilesetsChanged();
    emit mapChanged();
}

QString Editor::tileReflectionBlurMode(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return QStringLiteral("none");
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    return resourceEffectBlurMode(ts->tileResourceEffects.value(Tileset::priorityKey(canonical.x(), canonical.y())),
                                  QStringLiteral("reflection"));
}

int Editor::tileReflectionBlurStrength(int tilesetIdx, int tx, int ty) const
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return 0;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    return resourceEffectBlurStrength(ts->tileResourceEffects.value(Tileset::priorityKey(canonical.x(), canonical.y())),
                                      QStringLiteral("reflection"));
}

void Editor::setTileReflectionBlur(int tilesetIdx, int tx, int ty, const QString& mode, int strength)
{
    Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
    const QString key = Tileset::priorityKey(canonical.x(), canonical.y());
    QVector<TileResourceEffectBinding> effects = ts->tileResourceEffects.value(key);
    if (resourceEffectPreset(effects, QStringLiteral("reflection")).isEmpty()) return;
    QString normalized = mode.trimmed().toLower();
    if (normalized != QLatin1String("horizontal") && normalized != QLatin1String("vertical") &&
        normalized != QLatin1String("both")) normalized = QStringLiteral("none");
    const int amount = qBound(0, strength, 24);
    const int expected = normalized == QLatin1String("none") ? 0 : amount;
    if (resourceEffectBlurMode(effects, QStringLiteral("reflection")) == normalized &&
        resourceEffectBlurStrength(effects, QStringLiteral("reflection")) == expected) return;
    setResourceEffectBlur(effects, QStringLiteral("reflection"), normalized, amount);
    if (effects.isEmpty()) ts->tileResourceEffects.remove(key);
    else ts->tileResourceEffects.insert(key, effects);
    markDirty();
    emit tilesetsChanged();
    emit mapChanged();
}

// ------------------------------------------------------------- random pool
void Editor::addRectToRandomPool(int tilesetIdx, int x, int y, int w, int h)
{
    const Tileset* ts = tilesetAt(tilesetIdx);
    if (!ts) return;
    // Se o retangulo exato ja existe, remove (clique alterna) — igual ao JS.
    for (int i = 0; i < randomPool.size(); ++i) {
        const RandomEntry& e = randomPool[i];
        if (e.tilesetIdx == tilesetIdx && e.x == x && e.y == y && e.w == w && e.h == h) {
            randomPool.remove(i);
            emit tilesetsChanged();
            emit selectionChanged();
            return;
        }
    }
    RandomEntry e;
    e.tilesetIdx = tilesetIdx; e.x = x; e.y = y; e.w = w; e.h = h;
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx) {
            TileRef t; t.tilesetIdx = tilesetIdx; t.tx = x + dx; t.ty = y + dy;
            e.tiles.push_back(t);
            e.offsets.push_back(QPoint(dx, dy));
        }
    randomPool.push_back(e);
    markDirty();
    emit tilesetsChanged();
    emit selectionChanged();
}

bool Editor::isInRandomPool(int tilesetIdx, int tx, int ty) const
{
    for (const RandomEntry& e : randomPool)
        if (e.tilesetIdx == tilesetIdx && tx >= e.x && tx < e.x + e.w &&
            ty >= e.y && ty < e.y + e.h) return true;
    return false;
}

void Editor::clearRandomPool()
{
    randomPool.clear();
    emit tilesetsChanged();
    emit selectionChanged();
}

const RandomEntry* Editor::pickRandomEntry() const
{
    if (randomPool.isEmpty()) return nullptr;
    // Peso = media das probabilidades dos tiles da entrada (respeita Probability).
    QVector<double> weights;
    double total = 0;
    for (const RandomEntry& e : randomPool) {
        double w = 0;
        for (const TileRef& t : e.tiles) w += tileProb(t.tilesetIdx, t.tx, t.ty);
        w = e.tiles.isEmpty() ? 0 : w / e.tiles.size();
        weights.push_back(w);
        total += w;
    }
    if (total <= 0) return &randomPool[QRandomGenerator::global()->bounded(randomPool.size())];
    double r = QRandomGenerator::global()->generateDouble() * total;
    for (int i = 0; i < randomPool.size(); ++i) {
        r -= weights[i];
        if (r <= 0) return &randomPool[i];
    }
    return &randomPool.last();
}

TileRef Editor::pickRandomSingleTile() const
{
    QVector<TileRef> flat;
    for (const RandomEntry& e : randomPool) flat += e.tiles;
    if (flat.isEmpty()) return TileRef();
    double total = 0;
    QVector<double> w;
    for (const TileRef& t : flat) { const double p = tileProb(t.tilesetIdx, t.tx, t.ty); w.push_back(p); total += p; }
    if (total <= 0) return flat[QRandomGenerator::global()->bounded(flat.size())];
    double r = QRandomGenerator::global()->generateDouble() * total;
    for (int i = 0; i < flat.size(); ++i) { r -= w[i]; if (r <= 0) return flat[i]; }
    return flat.last();
}

// -------------------------------------------------------------------- stamp
Stamp Editor::currentStamp() const
{
    Stamp s;
    if (session.customStamp.valid()) {
        s.w = session.customStamp.w;
        s.h = session.customStamp.h;
        s.tiles = session.customStamp.tiles;
        s.offsets = session.customStamp.offsets;
        return s;
    }
    if (!session.tsSel.valid()) return Stamp{ 0, 0, {}, {} };
    const Tileset* ts = tilesetAt(session.tsSel.tilesetIdx);
    if (!ts) return Stamp{ 0, 0, {}, {} };
    s.w = session.tsSel.w;
    s.h = session.tsSel.h;
    for (int dy = 0; dy < session.tsSel.h; ++dy)
        for (int dx = 0; dx < session.tsSel.w; ++dx) {
            TileRef t;
            t.tilesetIdx = session.tsSel.tilesetIdx;
            t.tx = session.tsSel.x + dx;
            t.ty = session.tsSel.y + dy;
            s.tiles.push_back(t);
            s.offsets.push_back(QPoint(dx, dy));
        }
    return s;
}

QString Editor::savePattern(const QString& requestedName, const Stamp& stamp)
{
    const LayerPtr source = activeLayer();
    const bool objectSource = source && source->type == LayerType::Object;
    if (!objectSource && (!stamp.valid() || stamp.tiles.size() != stamp.offsets.size())) return {};
    const int tw = source && source->type == LayerType::Tile ? source->tileWidth : qMax(1, mapInfo().tileWidth);
    const int th = source && source->type == LayerType::Tile ? source->tileHeight : qMax(1, mapInfo().tileHeight);
    LayerPtr objects;
    QRectF bounds;
    int cols = stamp.w, rows = stamp.h;
    if (objectSource) {
        objects = cloneLayer(source, false);
        objects->objects.clear();
        const bool selected = !session.selectedObjectIds.isEmpty() || !session.selectedObjectId.isEmpty();
        for (const MapObject& object : source->objects) {
            if (!object.visible || (selected && !session.selectedObjectIds.contains(object.id) && session.selectedObjectId != object.id)) continue;
            objects->objects.push_back(object);
            const QRectF visualBounds = paint::objectBounds(object);
            bounds = bounds.isEmpty() ? visualBounds : bounds.united(visualBounds);
        }
        if (bounds.isEmpty()) return {};
        cols = qMax(1, int(std::ceil(bounds.width() / tw)));
        rows = qMax(1, int(std::ceil(bounds.height() / th)));
        objects->offsetx = objects->offsety = 0;
        objects->visible = true;
        objects->opacity = 1.0;
        objects->blendMode = QStringLiteral("source-over");
    }
    const qint64 width = qint64(cols) * tw, height = qint64(rows) * th;
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768 || width * height > 67108864) {
        emit status(QObject::tr("Seleção grande demais para converter em Pattern. Selecione uma área menor."), 6000);
        return {};
    }
    QImage image(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        if (objectSource) {
            painter.translate(-bounds.topLeft());
            RenderOptions options;
            options.drawObjectFrames = false;
            options.tileFilter = [](const TileRef&) { return true; };
            drawLayer(painter, *this, objects, 1.0, options);
        } else {
            for (int i = 0; i < stamp.tiles.size(); ++i) {
                const TileRef& tile = stamp.tiles[i];
                const Tileset* ts = tilesetAt(tile.tilesetIdx);
                const QPoint offset = stamp.offsets[i];
                if (!ts || !ts->contains(tile.tx, tile.ty) || offset.x() < 0 || offset.y() < 0 || offset.x() >= cols || offset.y() >= rows) return {};
                painter.drawImage(QRect(offset.x() * tw, offset.y() * th, tw, th), ts->image, ts->tileRect(tile.tx, tile.ty));
            }
        }
    }
    const QString name = requestedName.trimmed().isEmpty() ? QObject::tr("Pattern %1").arg(savedStamps.size() + 1) : requestedName.trimmed();
    Tileset generated = makeTileset(image, name, tw, th, 0, 0);
    generated.generatedFromBake = true;
    generated.category = QStringLiteral("Convertidos");
    // Flattening stacks preserves the strongest priority and blocked sides.
    if (!objectSource) for (int i = 0; i < stamp.tiles.size(); ++i) {
        const TileRef& tile = stamp.tiles[i];
        const QPoint offset = stamp.offsets[i];
        const QString key = Tileset::priorityKey(offset.x(), offset.y());
        generated.tilePriorities[key] = qMax(generated.tilePriorities.value(key), tilePriority(tile.tilesetIdx, tile.tx, tile.ty));
        generated.tileCollisionMasks[key] = generated.tileCollisionMasks.value(key) | collisionMask(tile.tilesetIdx, tile.tx, tile.ty);
    }
    if (objectSource) for (const MapObject& object : objects->objects) {
        const int sw = qMax(1,object.stampW), sh = qMax(1,object.stampH);
        for (int i=0;i<object.tiles.size();++i) {
            const TileRef& tile = object.tiles[i];
            const QRectF rect(object.x-bounds.x()+(i%sw)*object.w/sw,
                              object.y-bounds.y()+(i/sw)*object.h/sh, object.w/sw,object.h/sh);
            const int priority = tilePriority(tile.tilesetIdx,tile.tx,tile.ty);
            const int collision = collisionMask(tile.tilesetIdx,tile.tx,tile.ty);
            for (int y=qMax(0,int(std::floor(rect.top()/th))); y<qMin(rows,int(std::ceil(rect.bottom()/th)));++y)
                for(int x=qMax(0,int(std::floor(rect.left()/tw)));x<qMin(cols,int(std::ceil(rect.right()/tw)));++x) {
                    generated.setTilePriority(x,y,qMax(generated.tilePriority(x,y),priority));
                    generated.setTileCollisionMask(x,y,generated.tileCollisionMask(x,y)|collision);
                }
        }
    }
    QVector<Tileset> parts = splitTilesetForTextureLimit(generated, 4096);
    SavedStamp saved;
    saved.name = name;
    saved.stamp.w = cols; saved.stamp.h = rows;
    for (Tileset& part : parts) {
        const int idx = tilesets.size();
        tilesets.push_back(part);
        for (int y = 0; y < part.rows; ++y) for (int x = 0; x < part.columns; ++x) {
            saved.stamp.tiles.push_back(TileRef{idx, x, y});
            saved.stamp.offsets.push_back(QPoint(part.sourceTileX + x, part.sourceTileY + y));
        }
    }
    const QString id = saved.id;
    savedStamps.push_back(saved);
    reindexTilesetGids();
    markDirty();
    emit tilesetsChanged();
    emit patternsChanged();
    emit projectChanged();
    return id;
}

bool Editor::applyPattern(const QString& id)
{
    for (const SavedStamp& saved : savedStamps) {
        if (saved.id != id || !saved.stamp.valid()) continue;

        // Nunca exponha um Pattern estruturalmente inconsistente ao Renderer.
        // Isso também protege projetos antigos/corrompidos e patterns que
        // referenciem um tileset que foi removido depois de serem salvos.
        if (saved.stamp.tiles.size() != saved.stamp.offsets.size()) return false;
        for (int i = 0; i < saved.stamp.tiles.size(); ++i) {
            const TileRef& ref = saved.stamp.tiles[i];
            const QPoint off = saved.stamp.offsets[i];
            const Tileset* ts = tilesetAt(ref.tilesetIdx);
            if (!ts || ref.tx < 0 || ref.ty < 0 || ref.tx >= ts->columns || ref.ty >= ts->rows)
                return false;
            if (off.x() < 0 || off.y() < 0 || off.x() >= saved.stamp.w || off.y() >= saved.stamp.h)
                return false;
        }

        // Copia por valor ANTES do sinal. selectionChanged() é síncrono e pode
        // reconstruir widgets/listas; o estado aplicado não pode depender de
        // referências para elementos de savedStamps durante essa emissão.
        const Stamp stamp = saved.stamp;
        session.customStamp.w = stamp.w;
        session.customStamp.h = stamp.h;
        session.customStamp.tiles = stamp.tiles;
        session.customStamp.offsets = stamp.offsets;
        session.activeAutotileId.clear();
        session.wangBrushActive = false;
        session.regionMarkMode = session.starMarkMode = session.collisionMarkMode = false;
        session.randomMode = false;
        session.tool = activeLayer() && activeLayer()->type == LayerType::Object ? Tool::Object : Tool::Stamp;
        session.authoringContext = AuthoringContext::Pattern;
        emit selectionChanged();
        return true;
    }
    return false;
}

bool Editor::renamePattern(const QString& id, const QString& requestedName)
{
    const QString name = requestedName.trimmed();
    if (name.isEmpty()) return false;
    for (SavedStamp& saved : savedStamps) {
        if (saved.id != id) continue;
        if (saved.name == name) return true;
        saved.name = name;
        markDirty();
        emit patternsChanged();
        emit projectChanged();
        return true;
    }
    return false;
}

bool Editor::removePattern(const QString& id)
{
    for (int i = 0; i < savedStamps.size(); ++i) {
        if (savedStamps[i].id != id) continue;
        savedStamps.remove(i);
        markDirty();
        emit patternsChanged();
        emit projectChanged();
        return true;
    }
    return false;
}


// --------------------------------------------------------------------- Wang
WangSet* Editor::activeWangSet()
{
    if (session.activeWangSetIdx < 0 || session.activeWangSetIdx >= wangSets.size()) return nullptr;
    return &wangSets[session.activeWangSetIdx];
}
const WangSet* Editor::activeWangSet() const
{
    if (session.activeWangSetIdx < 0 || session.activeWangSetIdx >= wangSets.size()) return nullptr;
    return &wangSets[session.activeWangSetIdx];
}
void Editor::setActiveWangSet(int idx)
{
    session.activeWangSetIdx = (idx >= 0 && idx < wangSets.size()) ? idx : -1;
    const WangSet* ws = activeWangSet();
    if (ws && !ws->colors.isEmpty()) {
        bool ok = false;
        for (const WangColor& c : ws->colors) if (c.id == session.activeWangColorId) { ok = true; break; }
        if (!ok) session.activeWangColorId = ws->colors.first().id;
    }
    emit wangChanged();
}

// Histórico/Undo foi isolado em EditorHistory.cpp.

void Editor::markDirty()
{
    MapDoc* d = doc();
    if (d) { d->dirty = true; d->rpgMakerImported = false; }
    if (!projectDirty) { projectDirty = true; emit projectChanged(); }
}

void Editor::markSaved()
{
    for (MapDoc& d : docs) d.dirty = false;
    projectDirty = false;
    emit projectChanged();
}


} // namespace core
