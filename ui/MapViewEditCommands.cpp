#include "MapView.h"

#include "core/Renderer.h"
#include "core/RegionGradient.h"
#include "core/EditorInputPolicy.h"
#include "core/LayerTree.h"
#include "core/TilesetCatalog.h"
#include "core/Wang.h"

#include <QApplication>
#include <QTimer>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QHash>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QStack>
#include <QUuid>
#include <QWheelEvent>
#include <QSvgRenderer>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

using namespace core;

namespace ui {
namespace {

bool supportsRasterMaskForEdit(const LayerPtr& layer)
{
    return layer && (layer->type == LayerType::Image || layer->type == LayerType::Tile);
}

bool isEditingRasterMaskForEdit(const Editor& ed, const LayerPtr& layer)
{
    return supportsRasterMaskForEdit(layer) && ed.session.selectedMaskLayerId == layer->id &&
           !layer->imageMask.isNull();
}

QSize rasterMaskSizeForEdit(const LayerPtr& layer)
{
    if (!layer) return QSize();
    if (layer->type == LayerType::Image) return layer->image.size();
    if (layer->type == LayerType::Tile)
        return QSize(qMax(1, layer->cols * layer->tileWidth), qMax(1, layer->rows * layer->tileHeight));
    return QSize();
}

QRect normalizedCellRectForEdit(const QPoint& a, const QPoint& b)
{
    return QRect(a, b).normalized();
}

} // namespace

bool MapView::rasterEditContext(LayerPtr* layerOut, QImage** imageOut, bool* maskOut) const
{
    LayerPtr layer = ed.activeLayer();
    if (!layer) return false;
    const bool mask = isEditingRasterMaskForEdit(ed, layer);
    const bool paint = layer->type == LayerType::Image && (layer->imagePaintLayer || layer->alphaLock);
    if (!mask && !paint) return false;

    if (mask && (layer->imageMask.isNull() || layer->imageMask.size() != rasterMaskSizeForEdit(layer))) {
        // A máscara nasce branca (100% visível), igual ao fluxo do pincel.
        QImage fresh(rasterMaskSizeForEdit(layer), QImage::Format_ARGB32_Premultiplied);
        fresh.fill(Qt::white);
        if (!layer->imageMask.isNull()) {
            QPainter painter(&fresh);
            painter.drawImage(QPoint(0, 0), layer->imageMask);
        }
        layer->imageMask = fresh;
    }

    if (layerOut) *layerOut = layer;
    if (imageOut) *imageOut = mask ? &layer->imageMask : &layer->image;
    if (maskOut) *maskOut = mask;
    return true;
}

QRect MapView::activeCellSelection() const
{
    if (m_editCellSelection.isEmpty()) return QRect();
    if (ed.session.regionMarkMode)
        return m_editSelectionLayerId == QLatin1String("__regions__") ? m_editCellSelection : QRect();
    const LayerPtr layer = ed.activeLayer();
    if (!layer || layer->type != LayerType::Tile || m_editSelectionLayerId != layer->id) return QRect();
    return m_editCellSelection.intersected(QRect(0, 0, layer->cols, layer->rows));
}

QRect MapView::activeRasterSelection() const
{
    LayerPtr layer;
    QImage* image = nullptr;
    if (!rasterEditContext(&layer, &image, nullptr) || !image) return QRect();
    if (m_editSelectionLayerId != layer->id || m_editRasterSelection.isEmpty()) return QRect();
    return m_editRasterSelection.intersected(image->rect());
}

void MapView::beginEditSelection(const QPointF& mapPos)
{
    m_editSelecting = true;
    m_editCellSelection = QRect();
    m_editRasterSelection = QRect();
    m_editSelectStartMap = mapPos;

    if (ed.session.regionMarkMode) {
        m_editSelectionLayerId = QStringLiteral("__regions__");
        m_editSelectStartCell = regionCellAt(mapPos);
        m_editCellSelection = QRect(m_editSelectStartCell, QSize(1, 1));
    } else if (const LayerPtr layer = ed.activeLayer(); layer && layer->type == LayerType::Tile) {
        m_editSelectionLayerId = layer->id;
        m_editSelectStartCell = cellAt(mapPos, layer);
        m_editCellSelection = normalizedCellRectForEdit(m_editSelectStartCell, m_editSelectStartCell)
                                  .intersected(QRect(0, 0, layer->cols, layer->rows));
    } else {
        LayerPtr rasterLayer;
        QImage* image = nullptr;
        if (rasterEditContext(&rasterLayer, &image, nullptr) && image) {
            m_editSelectionLayerId = rasterLayer->id;
            const QPointF local = rasterLayer->type == LayerType::Image
                                      ? mapToImageLocalPoint(rasterLayer, mapPos)
                                      : mapToLayerPoint(rasterLayer, mapPos);
            const QPoint px(qFloor(local.x()), qFloor(local.y()));
            m_editRasterSelection = QRect(px, QSize(1, 1)).intersected(image->rect());
        } else {
            m_editSelecting = false;
            m_editSelectionLayerId.clear();
        }
    }
    update();
}

void MapView::updateEditSelection(const QPointF& mapPos)
{
    if (!m_editSelecting) return;
    if (ed.session.regionMarkMode && m_editSelectionLayerId == QLatin1String("__regions__")) {
        const MapDoc* doc = ed.doc();
        if (!doc) return;
        const QPoint end = regionCellAt(mapPos);
        m_editCellSelection = normalizedCellRectForEdit(m_editSelectStartCell, end)
                                  .intersected(QRect(0, 0, doc->map.width, doc->map.height));
    } else if (const LayerPtr layer = ed.activeLayer(); layer && layer->type == LayerType::Tile &&
               m_editSelectionLayerId == layer->id) {
        const QPoint end = cellAt(mapPos, layer);
        m_editCellSelection = normalizedCellRectForEdit(m_editSelectStartCell, end)
                                  .intersected(QRect(0, 0, layer->cols, layer->rows));
    } else {
        LayerPtr rasterLayer;
        QImage* image = nullptr;
        if (!rasterEditContext(&rasterLayer, &image, nullptr) || !image || m_editSelectionLayerId != rasterLayer->id) return;
        const QPointF a = rasterLayer->type == LayerType::Image
                              ? mapToImageLocalPoint(rasterLayer, m_editSelectStartMap)
                              : mapToLayerPoint(rasterLayer, m_editSelectStartMap);
        const QPointF b = rasterLayer->type == LayerType::Image
                              ? mapToImageLocalPoint(rasterLayer, mapPos)
                              : mapToLayerPoint(rasterLayer, mapPos);
        QRectF rf(a, b);
        rf = rf.normalized();
        if (rf.width() < 1.0) rf.setWidth(1.0);
        if (rf.height() < 1.0) rf.setHeight(1.0);
        m_editRasterSelection = rf.toAlignedRect().intersected(image->rect());
    }
    update();
}

void MapView::finishEditSelection(const QPointF& mapPos)
{
    if (!m_editSelecting) return;
    updateEditSelection(mapPos);
    m_editSelecting = false;
    emit statusMessage(tr("Seleção pronta. Ctrl+C copia · Ctrl+X recorta · Delete apaga · Esc limpa."));
    update();
}

bool MapView::editSelectAll()
{
    if (ed.session.regionMarkMode) {
        const MapDoc* doc = ed.doc();
        if (!doc) return false;
        m_editSelectionLayerId = QStringLiteral("__regions__");
        m_editCellSelection = QRect(0, 0, doc->map.width, doc->map.height);
        m_editRasterSelection = QRect();
        update();
        emit statusMessage(tr("Todas as regiões do mapa foram selecionadas."));
        return true;
    }

    const LayerPtr layer = ed.activeLayer();
    if (!layer) return false;
    if (layer->type == LayerType::Object) {
        ed.session.selectedObjectIds.clear();
        for (const MapObject& object : layer->objects) ed.session.selectedObjectIds.insert(object.id);
        ed.session.selectedObjectId = ed.session.selectedObjectIds.isEmpty() ? QString() : *ed.session.selectedObjectIds.begin();
        emit ed.selectionChanged();
        update();
        emit statusMessage(tr("Todos os objetos da camada foram selecionados."));
        return true;
    }
    if (layer->type == LayerType::Tile) {
        m_editSelectionLayerId = layer->id;
        m_editCellSelection = QRect(0, 0, layer->cols, layer->rows);
        m_editRasterSelection = QRect();
        update();
        emit statusMessage(tr("Todos os tiles da camada foram selecionados."));
        return true;
    }
    LayerPtr rasterLayer;
    QImage* image = nullptr;
    if (rasterEditContext(&rasterLayer, &image, nullptr) && image) {
        m_editSelectionLayerId = rasterLayer->id;
        m_editRasterSelection = image->rect();
        m_editCellSelection = QRect();
        update();
        emit statusMessage(tr("Todo o conteúdo de pintura foi selecionado."));
        return true;
    }
    if (layer->type == LayerType::Image) {
        ed.setSelectedLayerById(layer->id);
        emit statusMessage(tr("Camada de imagem selecionada."));
        return true;
    }
    return false;
}

bool MapView::editCopy()
{
    if (ed.session.regionMarkMode) {
        const MapDoc* doc = ed.doc();
        if (!doc) return false;
        const QRect selection = activeCellSelection();
        if (selection.isEmpty()) {
            emit statusMessage(tr("Selecione uma área de regiões primeiro (Seleção ou Ctrl+A)."));
            return false;
        }
        m_regionClipboard.clear();
        m_regionClipboardSize = selection.size();
        m_regionClipboard.reserve(selection.width() * selection.height());
        for (int y = selection.top(); y <= selection.bottom(); ++y)
            for (int x = selection.left(); x <= selection.right(); ++x)
                m_regionClipboard.push_back(doc->regionIdAt(x, y));
        emit statusMessage(tr("Regiões copiadas: %1 × %2.").arg(selection.width()).arg(selection.height()));
        return true;
    }

    const LayerPtr layer = ed.activeLayer();
    if (!layer) return false;
    if (layer->type == LayerType::Object) {
        ed.session.clipboardObjects.clear();
        for (const MapObject& object : layer->objects)
            if (ed.session.selectedObjectIds.contains(object.id)) ed.session.clipboardObjects.push_back(object);
        if (ed.session.clipboardObjects.isEmpty()) return false;
        emit statusMessage(tr("Objetos copiados: %1.").arg(ed.session.clipboardObjects.size()));
        return true;
    }
    if (layer->type == LayerType::Tile) {
        const QRect selection = activeCellSelection();
        if (selection.isEmpty()) {
            emit statusMessage(tr("Selecione uma área de tiles primeiro (Seleção ou Ctrl+A)."));
            return false;
        }
        m_tileClipboard.clear();
        m_tileClipboard.resize(selection.height());
        for (int y = 0; y < selection.height(); ++y) {
            m_tileClipboard[y].resize(selection.width());
            for (int x = 0; x < selection.width(); ++x)
                m_tileClipboard[y][x] = layer->cellAt(selection.x() + x, selection.y() + y);
        }
        emit statusMessage(tr("Tiles copiados: %1 × %2.").arg(selection.width()).arg(selection.height()));
        return true;
    }

    LayerPtr rasterLayer;
    QImage* image = nullptr;
    bool mask = false;
    if (rasterEditContext(&rasterLayer, &image, &mask) && image) {
        const QRect selection = activeRasterSelection();
        if (selection.isEmpty()) {
            emit statusMessage(tr("Selecione uma área de pintura primeiro (Seleção ou Ctrl+A)."));
            return false;
        }
        m_rasterClipboard = image->copy(selection);
        m_rasterClipboardIsMask = mask;
        emit statusMessage(tr("Área de pintura copiada: %1 × %2 px.").arg(selection.width()).arg(selection.height()));
        return !m_rasterClipboard.isNull();
    }

    if (layer->type == LayerType::Image) {
        ed.session.clipboardLayer = cloneLayer(layer, false);
        emit statusMessage(tr("Camada “%1” copiada.").arg(layer->name));
        return true;
    }
    return false;
}

bool MapView::editDelete()
{
    if (ed.session.regionMarkMode) {
        MapDoc* doc = ed.doc();
        if (!doc) return false;
        QRect selection = activeCellSelection();
        if (selection.isEmpty()) return false;
        const bool beforeAuthored = doc->rpgMakerRegionsAuthored;
        QVector<RegionHistoryChange> changes;
        for (int y = selection.top(); y <= selection.bottom(); ++y) {
            for (int x = selection.left(); x <= selection.right(); ++x) {
                const int before = doc->regionIdAt(x, y);
                if (before == 0) continue;
                changes.push_back(RegionHistoryChange{x, y, before, 0});
                doc->setRegionIdAt(x, y, 0);
            }
        }
        doc->rpgMakerRegionsAuthored = true;
        ed.pushRegionHistory(changes, beforeAuthored, tr("Apagar regiões selecionadas"));
        update();
        return true;
    }

    const LayerPtr layer = ed.activeLayer();
    if (!layer) return false;
    if (layer->type == LayerType::Object) {
        if (ed.session.selectedObjectIds.isEmpty()) return false;
        Editor::EditSession session = ed.beginLayerEdit(layer);
        for (int i = layer->objects.size() - 1; i >= 0; --i)
            if (ed.session.selectedObjectIds.contains(layer->objects[i].id)) layer->objects.remove(i);
        ed.session.selectedObjectIds.clear();
        ed.session.selectedObjectId.clear();
        ed.commitLayerEdit(session, tr("Excluir objetos selecionados"));
        emit ed.selectionChanged();
        update();
        return true;
    }
    if (layer->type == LayerType::Tile) {
        const QRect selection = activeCellSelection();
        if (selection.isEmpty()) return false;
        Editor::EditSession session = ed.beginLayerEdit(layer);
        for (int y = selection.top(); y <= selection.bottom(); ++y)
            for (int x = selection.left(); x <= selection.right(); ++x)
                layer->setCell(x, y, Cell());
        wang::retileTerrainNeighborhood(ed, layer, selection);
        invalidateMaskPathCache(layer->id);
        ed.commitLayerEdit(session, tr("Apagar tiles selecionados"));
        update();
        return true;
    }

    LayerPtr rasterLayer;
    QImage* image = nullptr;
    bool mask = false;
    if (rasterEditContext(&rasterLayer, &image, &mask) && image) {
        const QRect selection = activeRasterSelection();
        if (selection.isEmpty()) return false;
        Editor::EditSession session = ed.beginLayerEdit(rasterLayer);
        QPainter painter(image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(selection, mask ? QColor(0, 0, 0, 255) : QColor(0, 0, 0, 0));
        painter.end();
        invalidateMaskPathCache(rasterLayer->id);
        ed.commitLayerEdit(session, mask ? tr("Ocultar área selecionada da máscara")
                                         : tr("Apagar área de pintura selecionada"));
        update();
        return true;
    }
    if (layer->type == LayerType::Image && ed.session.tool == Tool::Select &&
        ed.selectedLayer() && ed.selectedLayer()->id == layer->id) {
        ed.removeLayer(layer->id);
        update();
        emit statusMessage(tr("Camada de imagem removida."));
        return true;
    }
    return false;
}

bool MapView::editCut()
{
    if (!editCopy()) return false;
    return editDelete();
}

bool MapView::editPaste()
{
    if (ed.session.regionMarkMode) {
        MapDoc* doc = ed.doc();
        if (!doc || m_regionClipboard.isEmpty() || m_regionClipboardSize.isEmpty()) return false;
        QPoint origin = activeCellSelection().isEmpty() ? m_hoverCell : activeCellSelection().topLeft();
        if (origin.x() < 0 || origin.y() < 0) origin = QPoint(0, 0);
        const bool beforeAuthored = doc->rpgMakerRegionsAuthored;
        QVector<RegionHistoryChange> changes;
        int index = 0;
        for (int y = 0; y < m_regionClipboardSize.height(); ++y) {
            for (int x = 0; x < m_regionClipboardSize.width(); ++x, ++index) {
                const int gx = origin.x() + x, gy = origin.y() + y;
                if (!doc->regionInBounds(gx, gy)) continue;
                const int before = doc->regionIdAt(gx, gy);
                const int after = m_regionClipboard.value(index);
                if (before == after) continue;
                changes.push_back(RegionHistoryChange{gx, gy, before, after});
                doc->setRegionIdAt(gx, gy, after);
            }
        }
        doc->rpgMakerRegionsAuthored = true;
        ed.pushRegionHistory(changes, beforeAuthored, tr("Colar regiões"));
        m_editSelectionLayerId = QStringLiteral("__regions__");
        m_editCellSelection = QRect(origin, m_regionClipboardSize).intersected(QRect(0, 0, doc->map.width, doc->map.height));
        update();
        return true;
    }

    const LayerPtr layer = ed.activeLayer();
    if (!layer) return false;
    if (layer->type == LayerType::Object) {
        if (ed.session.clipboardObjects.isEmpty()) return false;
        Editor::EditSession session = ed.beginLayerEdit(layer);
        ed.session.selectedObjectIds.clear();
        for (MapObject object : ed.session.clipboardObjects) {
            object.id = idGen();
            object.x += 16.0;
            object.y += 16.0;
            ed.session.selectedObjectIds.insert(object.id);
            layer->objects.push_back(object);
        }
        ed.session.selectedObjectId = ed.session.selectedObjectIds.isEmpty() ? QString() : *ed.session.selectedObjectIds.begin();
        ed.commitLayerEdit(session, tr("Colar objetos"));
        emit ed.selectionChanged();
        update();
        return true;
    }
    if (layer->type == LayerType::Tile) {
        if (m_tileClipboard.isEmpty() || m_tileClipboard.first().isEmpty()) return false;
        QPoint origin = activeCellSelection().isEmpty() ? m_hoverCell : activeCellSelection().topLeft();
        if (origin.x() < 0 || origin.y() < 0) origin = QPoint(0, 0);
        Editor::EditSession session = ed.beginLayerEdit(layer);
        const int h = m_tileClipboard.size();
        const int w = m_tileClipboard.first().size();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < m_tileClipboard[y].size(); ++x)
                if (layer->inBounds(origin.x() + x, origin.y() + y))
                    layer->setCell(origin.x() + x, origin.y() + y, m_tileClipboard[y][x]);
        m_editSelectionLayerId = layer->id;
        m_editCellSelection = QRect(origin, QSize(w, h)).intersected(QRect(0, 0, layer->cols, layer->rows));
        wang::retileTerrainNeighborhood(ed, layer, m_editCellSelection);
        invalidateMaskPathCache(layer->id);
        ed.commitLayerEdit(session, tr("Colar tiles"));
        update();
        return true;
    }

    LayerPtr rasterLayer;
    QImage* image = nullptr;
    bool mask = false;
    if (rasterEditContext(&rasterLayer, &image, &mask) && image && !m_rasterClipboard.isNull()) {
        QRect selection = activeRasterSelection();
        QPoint origin = selection.isEmpty()
                            ? (rasterLayer->type == LayerType::Image
                                   ? mapToImageLocalPoint(rasterLayer, m_hoverMap).toPoint()
                                   : mapToLayerPoint(rasterLayer, m_hoverMap).toPoint())
                            : selection.topLeft();
        if (origin.x() < 0 || origin.y() < 0) origin = QPoint(0, 0);
        Editor::EditSession session = ed.beginLayerEdit(rasterLayer);
        QPainter painter(image);
        painter.setCompositionMode(mask ? QPainter::CompositionMode_Source
                                        : QPainter::CompositionMode_SourceOver);
        painter.drawImage(origin, m_rasterClipboard);
        painter.end();
        invalidateMaskPathCache(rasterLayer->id);
        ed.commitLayerEdit(session, mask ? tr("Colar na máscara") : tr("Colar pintura"));
        m_editSelectionLayerId = rasterLayer->id;
        m_editRasterSelection = QRect(origin, m_rasterClipboard.size()).intersected(image->rect());
        update();
        return true;
    }

    if (ed.session.clipboardLayer && ed.session.clipboardLayer->type == LayerType::Image) {
        const DocSnapshot before = ed.snapshotDoc();
        LayerPtr copy = cloneLayer(ed.session.clipboardLayer, true);
        copy->name += tr(" (cópia)");
        const int bump = ed.session.snapObjects ? qMax(1, ed.session.snapGridSize) : 16;
        copy->offsetx += bump;
        copy->offsety += bump;
        ed.addLayer(copy);
        ed.pushDocHistory(before, tr("Colar camada de imagem"));
        update();
        return true;
    }
    return false;
}

bool MapView::editClearSelection()
{
    bool changed = m_editSelecting || !m_editCellSelection.isEmpty() || !m_editRasterSelection.isEmpty() ||
                   !ed.session.selectedObjectIds.isEmpty() || !ed.session.selectedObjectId.isEmpty();
    m_editSelecting = false;
    m_editSelectionLayerId.clear();
    m_editCellSelection = QRect();
    m_editRasterSelection = QRect();
    ed.session.selectedObjectIds.clear();
    ed.session.selectedObjectId.clear();
    if (changed) emit ed.selectionChanged();
    update();
    return changed;
}



} // namespace ui
