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

static bool supportsRasterMask(const LayerPtr& layer)
{
    return layer && (layer->type == LayerType::Image || layer->type == LayerType::Tile);
}

static bool isEditingRasterMask(const Editor& ed, const LayerPtr& layer)
{
    return supportsRasterMask(layer) && ed.session.selectedMaskLayerId == layer->id &&
           !layer->imageMask.isNull();
}

static QSize rasterMaskSizeFor(const LayerPtr& layer)
{
    if (!layer) return QSize();
    if (layer->type == LayerType::Image) return layer->image.size();
    if (layer->type == LayerType::Tile)
        return QSize(qMax(1, layer->cols * layer->tileWidth), qMax(1, layer->rows * layer->tileHeight));
    return QSize();
}

static QRegion tileContentRegion(const LayerPtr& layer)
{
    QRegion region;
    if (!layer || layer->type != LayerType::Tile) return region;
    QVector<QRect> rects;
    rects.reserve(layer->cols * layer->rows / 2);
    for (int y = 0; y < layer->rows; ++y) {
        for (int x = 0; x < layer->cols; ++x) {
            if (layer->cellAt(x, y).isEmpty()) continue;
            rects.push_back(QRect(x * layer->tileWidth, y * layer->tileHeight,
                                  layer->tileWidth, layer->tileHeight));
        }
    }
    if (!rects.isEmpty()) region.setRects(rects.constData(), rects.size());
    return region;
}

static QRegion imageAlphaRegion(const LayerPtr& layer)
{
    QRegion region;
    if (!layer || layer->type != LayerType::Image || layer->image.isNull()) return region;
    const QImage image = layer->image.convertToFormat(QImage::Format_ARGB32);
    QVector<QRect> spans;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        int start = -1;
        for (int x = 0; x <= image.width(); ++x) {
            const bool solid = x < image.width() && qAlpha(row[x]) > 0;
            if (solid && start < 0) start = x;
            if ((!solid || x == image.width()) && start >= 0) {
                spans.push_back(QRect(start, y, x - start, 1));
                start = -1;
            }
        }
    }
    if (!spans.isEmpty()) region.setRects(spans.constData(), spans.size());
    return region;
}

static QTransform imageLocalToMapTransform(const LayerPtr& layer)
{
    return core::imageLayerTransform(layer);
}

static QRectF imageVisualLocalRect(const LayerPtr& layer)
{
    const QSize size = core::visualLayerFrameSize(layer);
    return QRectF(QPointF(0, 0), QSizeF(size));
}

static QRectF imageLocalRectToMapRect(const LayerPtr& layer, const QRectF& localRect)
{
    return imageLocalToMapTransform(layer).mapRect(localRect);
}

static QRectF rasterLocalRectToMapRect(const LayerPtr& layer, const QRectF& localRect)
{
    if (layer && layer->type == LayerType::Image) return imageLocalRectToMapRect(layer, localRect);
    return localRect.translated(layer ? layer->offsetx : 0, layer ? layer->offsety : 0);
}

static QImage rasterBrushSilhouette(const RasterBrushSettings& brush, const QColor& outline,
                                    const QPointF& localCenter)
{
    const QImage tip = paint::rasterBrushPreviewTipAt(brush, localCenter).convertToFormat(QImage::Format_ARGB32);
    if (tip.isNull()) return QImage();
    QImage preview(tip.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    const QColor fill(outline.red(), outline.green(), outline.blue(), 42);
    const QColor edge(outline.red(), outline.green(), outline.blue(), 235);
    for (int y = 0; y < tip.height(); ++y) {
        QRgb* dst = reinterpret_cast<QRgb*>(preview.scanLine(y));
        const QRgb* src = reinterpret_cast<const QRgb*>(tip.constScanLine(y));
        for (int x = 0; x < tip.width(); ++x) {
            if (qAlpha(src[x]) <= 10) continue;
            const auto alphaAt = [&](int px, int py) {
                if (px < 0 || py < 0 || px >= tip.width() || py >= tip.height()) return 0;
                return qAlpha(tip.pixel(px, py));
            };
            const bool boundary = alphaAt(x-1,y) <= 10 || alphaAt(x+1,y) <= 10 ||
                                  alphaAt(x,y-1) <= 10 || alphaAt(x,y+1) <= 10;
            const QColor c = boundary ? edge : fill;
            dst[x] = qPremultiply(c.rgba());
        }
    }
    return preview;
}

namespace {
QRect regularPaintFootprint(const Editor& ed, int gx, int gy)
{
    int w = 1, h = 1;
    if (ed.session.customStamp.valid()) { w = qMax(1, ed.session.customStamp.w); h = qMax(1, ed.session.customStamp.h); }
    else if (ed.session.tsSel.valid()) { w = qMax(1, ed.session.tsSel.w); h = qMax(1, ed.session.tsSel.h); }
    return QRect(gx, gy, w, h);
}

QRect normalizedCellRect(const QPoint& a, const QPoint& b)
{
    return QRect(a, b).normalized();
}

QColor regionColor(int regionId, int alpha = 105)
{
    if (regionId <= 0) return QColor(0, 0, 0, 0);
    // Distribuição determinística de matiz: IDs vizinhos continuam fáceis de
    // distinguir sem tentar reproduzir a paleta interna do editor oficial.
    const int hue = (regionId * 47 + 13) % 360;
    return QColor::fromHsv(hue, 175, 235, qBound(0, alpha, 255));
}
}

MapView::MapView(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    m_animationClock.start();
    auto* animationTimer = new QTimer(this);
    animationTimer->setInterval(33);
    connect(animationTimer, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        bool animate = false;
        if (ed.session.animateAutotiles) {
            for (const Tileset& ts : ed.tilesets) {
                if (!ts.animatedAutotiles.isEmpty()) { animate = true; break; }
            }
        }
        if (!animate) {
            std::function<bool(const QVector<LayerPtr>&)> hasMovingVisual;
            hasMovingVisual = [&](const QVector<LayerPtr>& nodes) {
                for (const LayerPtr& layer : nodes) {
                    if (!layer || !layer->visible) continue;
                    if (layer->parallaxLayer &&
                        (layer->parallaxAnimationEnabled || qAbs(layer->parallaxSpeedX) > .001 ||
                         qAbs(layer->parallaxSpeedY) > .001 || layer->parallaxOscillationX > .001 ||
                         layer->parallaxOscillationY > .001)) return true;
                    if (hasMovingVisual(layer->children)) return true;
                }
                return false;
            };
            animate = hasMovingVisual(ed.layers());
        }
        if (animate) update();
    });
    animationTimer->start();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::CrossCursor);

    connect(&ed, &Editor::mapChanged, this, [this] {
        invalidateMaskPathCache();
        if (!m_suppressFullMapUpdate) update();
    });
    connect(&ed, &Editor::layersChanged, this, [this] {
        // Trocar/ocultar/excluir uma camada enquanto o mouse ainda esta numa
        // operacao nao pode deixar uma EditSession apontando para o alvo antigo.
        cancelLayerInteraction();
        updateHeldModifiers(QApplication::keyboardModifiers());
        invalidateMaskPathCache();
        update();
    });
    connect(&ed, &Editor::docsChanged, this, [this] {
        m_animationClock.restart();
        update();
    });
    connect(&ed, &Editor::selectionChanged, this, [this] { updateHeldModifiers(QApplication::keyboardModifiers()); update(); });
    connect(&ed, &Editor::tilesetsChanged,  this, [this] {
        m_animationClock.restart();
        pixmapCache().invalidate();
        update();
    });
}

// --------------------------------------------------------------- coordenadas
QPointF MapView::screenToMap(const QPointF& p) const
{
    return QPointF(p.x() / m_zoom + m_pan.x(), p.y() / m_zoom + m_pan.y());
}

QPointF MapView::mapToScreen(const QPointF& p) const
{
    return QPointF((p.x() - m_pan.x()) * m_zoom, (p.y() - m_pan.y()) * m_zoom);
}

QRectF MapView::visibleMapRect() const
{
    return QRectF(m_pan, QSizeF(width() / m_zoom, height() / m_zoom));
}

QRectF MapView::screenRectToMapRect(const QRect& screenRect) const
{
    if (screenRect.isEmpty()) return QRectF();
    const QPointF a = screenToMap(QPointF(screenRect.left(), screenRect.top()));
    const QPointF b = screenToMap(QPointF(screenRect.right() + 1, screenRect.bottom() + 1));
    return QRectF(a, b).normalized();
}

QRect MapView::mapRectToUpdateRect(const QRectF& mapRect, int marginPx) const
{
    if (!mapRect.isValid() || mapRect.isEmpty()) return QRect();
    const QRectF screen = QRectF(mapToScreen(mapRect.topLeft()),
                                 mapToScreen(mapRect.bottomRight())).normalized();
    return screen.toAlignedRect().adjusted(-marginPx, -marginPx, marginPx, marginPx)
                 .intersected(rect());
}

void MapView::updateMapRect(const QRectF& mapRect, int marginPx)
{
    const QRect dirty = mapRectToUpdateRect(mapRect, marginPx);
    if (!dirty.isEmpty()) update();
}

QRectF MapView::tileCellMapRect(const LayerPtr& layer, const QPoint& cell) const
{
    if (!layer || layer->type != LayerType::Tile) return QRectF();
    return QRectF(cell.x() * layer->tileWidth + layer->offsetx,
                  cell.y() * layer->tileHeight + layer->offsety,
                  layer->tileWidth, layer->tileHeight);
}

QPointF MapView::mapToLayerPoint(const LayerPtr& layer, const QPointF& mapPos) const
{
    if (!layer) return mapPos;
    return mapPos - QPointF(layer->offsetx, layer->offsety);
}

QPointF MapView::mapToImageLocalPoint(const LayerPtr& layer, const QPointF& mapPos) const
{
    if (!layer || layer->type != LayerType::Image || layer->image.isNull())
        return mapToLayerPoint(layer, mapPos);

    const QTransform transform=core::imageLayerTransform(layer);
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    return invertible ? inverse.map(mapPos) : mapToLayerPoint(layer, mapPos);
}

QRectF MapView::objectMapRect(const LayerPtr& layer, const MapObject& object) const
{
    const QPointF offset = layer ? QPointF(layer->offsetx, layer->offsety) : QPointF();
    return paint::objectBounds(object).translated(offset);
}

QRectF MapView::cellMapRect(const QPoint& cell) const
{
    const LayerPtr l = ed.activeLayer();
    const MapInfo& info = ed.mapInfo();
    if (!ed.session.regionMarkMode && l && l->type == LayerType::Tile)
        return tileCellMapRect(l, cell);
    return QRectF(cell.x() * info.tileWidth, cell.y() * info.tileHeight,
                  info.tileWidth, info.tileHeight);
}

QRectF MapView::hoverVisualMapRect(const QPoint& cell, const QPointF& mapPos) const
{
    // A area cinza ao redor do mapa tambem e area de AUTORIA. Uma origem
    // negativa e valida para o ghost de um stamp/pattern grande; na aplicacao,
    // PaintOps ja recorta cada celula contra os limites reais da Tile Layer.
    const LayerPtr l = ed.activeLayer();
    const MapInfo& info = ed.mapInfo();
    if (ed.session.regionMarkMode) return QRectF(cell.x() * info.tileWidth, cell.y() * info.tileHeight,
                                         info.tileWidth, info.tileHeight);
    if (!l) return cellMapRect(cell);

    if (l->type == LayerType::Object && ed.session.tool == Tool::Object) {
        const Stamp stamp = ed.currentStamp();
        const int gw = qMax(1, info.tileWidth);
        const int gh = qMax(1, info.tileHeight);
        // Objetos sao armazenados em coordenadas locais da camada. O preview
        // precisa usar o mesmo snap local e so depois voltar ao espaco do mapa,
        // senao camadas com offset mostram o ghost numa posicao e criam noutra.
        QPointF local = mapToLayerPoint(l, mapPos);
        if (ed.session.snapObjects || ed.session.heldSnap) {
            const int g = qMax(1, ed.session.snapGridSize);
            local.setX(std::floor(local.x() / g) * g);
            local.setY(std::floor(local.y() / g) * g);
        }
        const QPointF visual = local + QPointF(l->offsetx, l->offsety);
        const int sw = stamp.valid() ? stamp.w : 1;
        const int sh = stamp.valid() ? stamp.h : 1;
        return QRectF(visual.x(), visual.y(), sw * gw, sh * gh);
    }

    const bool rasterTarget = isEditingRasterMask(ed, l) ||
        (l->type == LayerType::Image && (l->imagePaintLayer || l->alphaLock));
    if (rasterTarget && (ed.session.tool == Tool::Paint || ed.session.tool == Tool::Eraser)) {
        const QPointF local = l->type == LayerType::Image ? mapToImageLocalPoint(l, mapPos)
                                                          : mapToLayerPoint(l, mapPos);
        return rasterLocalRectToMapRect(l, paint::rasterBrushTargetRect(ed.session.rasterBrush, local));
    }

    if (l->type != LayerType::Tile) return cellMapRect(cell);
    const int tw = qMax(1, l->tileWidth), th = qMax(1, l->tileHeight);
    const int brushRadius = qMax(0, ed.session.brush.size - 1);
    int left = cell.x() - brushRadius;
    int top = cell.y() - brushRadius;
    int right = cell.x() + brushRadius;
    int bottom = cell.y() + brushRadius;
    if (ed.session.tool != Tool::Eraser && ed.session.tool != Tool::Terrain) {
        const Stamp stamp = ed.currentStamp();
        if (stamp.valid()) {
            right += qMax(0, stamp.w - 1);
            bottom += qMax(0, stamp.h - 1);
        }
    }
    if (ed.session.tool == Tool::Terrain || semanticAutotileActive()) {
        --left; --top; ++right; ++bottom; // Wang pode recalcular vizinhos imediatos.
    }
    return QRectF(left * tw + l->offsetx, top * th + l->offsety,
                  (right - left + 1) * tw, (bottom - top + 1) * th);
}

QRectF MapView::shapePreviewMapRect() const
{
    if (!m_shapeActive) return QRectF();
    const LayerPtr l = ed.activeLayer();
    if (!l || l->type != LayerType::Tile) return QRectF();
    const int x0 = qMin(m_shapeStart.x(), m_shapeEnd.x());
    const int x1 = qMax(m_shapeStart.x(), m_shapeEnd.x());
    const int y0 = qMin(m_shapeStart.y(), m_shapeEnd.y());
    const int y1 = qMax(m_shapeStart.y(), m_shapeEnd.y());
    return QRectF(x0 * l->tileWidth + l->offsetx, y0 * l->tileHeight + l->offsety,
                  (x1 - x0 + 1) * l->tileWidth, (y1 - y0 + 1) * l->tileHeight);
}

QRectF MapView::selectedObjectBounds(const LayerPtr& layer) const
{
    QRectF bounds;
    if (!layer || layer->type != LayerType::Object) return bounds;
    bool any = false;
    for (const MapObject& o : layer->objects) {
        if (!ed.session.selectedObjectIds.contains(o.id) && ed.session.selectedObjectId != o.id) continue;
        const QRectF visual = objectMapRect(layer, o);
        bounds = any ? bounds.united(visual) : visual;
        any = true;
    }
    return bounds;
}

QPoint MapView::cellAt(const QPointF& mapPos, const LayerPtr& layer) const
{
    const int tw = layer && layer->type == LayerType::Tile ? layer->tileWidth : ed.mapInfo().tileWidth;
    const int th = layer && layer->type == LayerType::Tile ? layer->tileHeight : ed.mapInfo().tileHeight;
    const QPointF local = layer && layer->type == LayerType::Tile ? mapToLayerPoint(layer, mapPos) : mapPos;
    return QPoint(int(std::floor(local.x() / qMax(1, tw))),
                  int(std::floor(local.y() / qMax(1, th))));
}

QPoint MapView::regionCellAt(const QPointF& mapPos) const
{
    const MapInfo& info = ed.mapInfo();
    return QPoint(int(std::floor(mapPos.x() / qMax(1, info.tileWidth))),
                  int(std::floor(mapPos.y() / qMax(1, info.tileHeight))));
}

void MapView::cancelLayerInteraction()
{
    // Se o alvo ainda existe, preserva a edicao parcial como uma operacao
    // normal de historico. Se foi removido, apenas invalida a sessao: gravar
    // historico para uma layer inexistente criaria undo inconsistente.
    if (m_session.valid && m_session.layer) {
        if (ed.findNode(m_session.layer->id))
            ed.commitLayerEdit(m_session, tr("Finalizar edição da camada"));
        else
            m_session.valid = false;
    }
    if (m_rightEraseSession.valid && m_rightEraseSession.layer) {
        if (ed.findNode(m_rightEraseSession.layer->id))
            ed.commitLayerEdit(m_rightEraseSession, tr("Finalizar borracha direita"));
        else m_rightEraseSession.valid = false;
    }
    if (m_gridFreeSession.valid && m_gridFreeSession.layer) {
        if (ed.findNode(m_gridFreeSession.layer->id))
            ed.commitLayerEdit(m_gridFreeSession, tr("Finalizar Aleatório Sem grade"));
        else m_gridFreeSession.valid = false;
    }
    if (m_rasterSession.valid && m_rasterSession.layer) {
        if (ed.findNode(m_rasterSession.layer->id))
            ed.commitLayerEdit(m_rasterSession, tr("Finalizar pintura"));
        else m_rasterSession.valid = false;
    }
    if (!m_imageDragStartOffsets.isEmpty()) {
        m_imageDragStartOffsets.clear();
        ed.pushDocHistory(m_imageGroupDragBefore, tr("Finalizar movimento das camadas"));
        emit ed.mapChanged();
        emit ed.layersChanged();
    } else if (m_imageDragSession.valid && m_imageDragSession.layer) {
        if (ed.findNode(m_imageDragSession.layer->id))
            ed.commitLayerEdit(m_imageDragSession, tr("Finalizar movimento da imagem"));
        else m_imageDragSession.valid = false;
    }
    m_imageDragging = false;
    m_imageTransformHandle = 0;
    m_imageDragLayerId.clear();
    m_imageDragSession.valid = false;
    m_rasterPainting = false;
    m_rasterErasing = false;
    m_rasterSession.valid = false;
    m_rasterSpacingCarry = 0.0;
    m_rasterUseClipRegion = false;
    m_rasterClipRegion = QRegion();
    m_painting = false;
    m_erasing = false;
    m_shapeActive = false;
    m_tilePickDrag = false;
    m_rightEraseCandidate = false;
    m_rightEraseActive = false;
    m_rightEraseSession.valid = false;
    m_gridFreePainting = false;
    m_gridFreeLayerId.clear();
    m_gridFreeSession.valid = false;
    m_slopeSelecting = false;
    m_regionPainting = false;
    m_regionShapeActive = false;
    m_editSelecting = false;
    m_dragObjectId.clear();
    m_dragHandle = -1;
    m_rotatingObject = false;
    m_marquee = false;
    m_lastCell = QPoint(-1, -1);
    m_randomScatterVisited.clear();
}


// Edição global (seleção/cut/copy/paste/delete) vive em MapViewEditCommands.cpp.

void MapView::clampPan()
{
    const MapInfo& info = ed.mapInfo();
    const double vw = width() / m_zoom, vh = height() / m_zoom;
    const double maxX = qMax(0.0, info.pixelWidth()  - vw * 0.5);
    const double maxY = qMax(0.0, info.pixelHeight() - vh * 0.5);
    m_pan.setX(clampd(m_pan.x(), -vw * 0.5, maxX));
    m_pan.setY(clampd(m_pan.y(), -vh * 0.5, maxY));
}

void MapView::setZoom(double z, const QPointF& anchorScreen)
{
    const double nz = clampd(z, 0.05, 16.0);
    if (qFuzzyCompare(nz, m_zoom)) return;
    const QPointF anchor = anchorScreen.x() >= 0 ? anchorScreen
                                                 : QPointF(width() / 2.0, height() / 2.0);
    const QPointF before = screenToMap(anchor);
    m_zoom = nz;
    ed.session.zoom = nz;
    const QPointF after = screenToMap(anchor);
    m_pan += before - after;
    clampPan();
    emit zoomChanged(m_zoom);
    emit viewportChanged();
    update();
}

void MapView::fitToView()
{
    const MapInfo& info = ed.mapInfo();
    if (info.pixelWidth() <= 0 || info.pixelHeight() <= 0) return;
    const double zx = double(width() - 24) / info.pixelWidth();
    const double zy = double(height() - 24) / info.pixelHeight();
    m_zoom = clampd(qMin(zx, zy), 0.05, 16.0);
    ed.session.zoom = m_zoom;
    m_pan = QPointF((info.pixelWidth()  - width() / m_zoom) / 2.0,
                    (info.pixelHeight() - height() / m_zoom) / 2.0);
    emit zoomChanged(m_zoom);
    emit viewportChanged();
    update();
}

void MapView::centerOn(const QPointF& mapPos)
{
    m_pan = QPointF(mapPos.x() - width() / (2.0 * m_zoom), mapPos.y() - height() / (2.0 * m_zoom));
    clampPan();
    emit viewportChanged();
    update();
}

QPointF MapView::viewCenterInMap() const
{
    return screenToMap(QPointF(width() / 2.0, height() / 2.0));
}

void MapView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    emit viewportChanged();
}

// -------------------------------------------------------------------- pintura
void MapView::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    const QRect screenDirty = event ? event->rect() : rect();
    p.fillRect(screenDirty, QColor("#1b1b1b"));

    const MapInfo& info = ed.mapInfo();
    const QRectF viewportMap = visibleMapRect();
    // Intersecta o repaint pedido pelo Qt com a viewport lógica. Assim um
    // hover/brush local não faz Renderer/Fog/Markers reconsiderarem a viewport inteira.
    QRectF paintMapRect = viewportMap;
    if (event && !event->region().isEmpty()) {
        const QRectF requested = screenRectToMapRect(event->region().boundingRect());
        if (requested.isValid() && !requested.isEmpty())
            paintMapRect = viewportMap.intersected(requested.adjusted(-2.0 / m_zoom, -2.0 / m_zoom,
                                                                       2.0 / m_zoom,  2.0 / m_zoom));
    }

    p.save();
    p.scale(m_zoom, m_zoom);
    p.translate(-m_pan);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setClipRect(paintMapRect, Qt::IntersectClip);

    // Fundo quadriculado de transparência, como nos editores 2D clássicos.
    // Fica apenas no editor: a cor real do mapa continua sendo `info.background`.
    const QRectF mapRect(0, 0, info.pixelWidth(), info.pixelHeight());
    if (ed.session.checkerboardBackground) {
        const int cell = qMax(4, qMin(info.tileWidth, info.tileHeight) / 2);
        p.fillRect(mapRect, ed.session.checkerColorA);
        const int x0 = qMax(0, int(std::floor(paintMapRect.left() / cell)) * cell);
        const int y0 = qMax(0, int(std::floor(paintMapRect.top() / cell)) * cell);
        const int x1 = qMin(info.pixelWidth(), int(std::ceil(paintMapRect.right() / cell)) * cell);
        const int y1 = qMin(info.pixelHeight(), int(std::ceil(paintMapRect.bottom() / cell)) * cell);
        p.setPen(Qt::NoPen);
        p.setBrush(ed.session.checkerColorB);
        for (int y = y0; y < y1; y += cell)
            for (int x = x0; x < x1; x += cell)
                if (((x / cell) + (y / cell)) & 1)
                    p.drawRect(QRectF(x, y, cell, cell));
        QColor bg = info.background; bg.setAlpha(qMin(bg.alpha(), 36));
        p.fillRect(mapRect, bg);
    } else {
        p.fillRect(mapRect, info.background);
    }

    // O canvas editorial e limitado ao retangulo real do mapa. Camadas com
    // offset continuam funcionando, mas nao "vazam" para o workspace cinza
    // (o export offscreen ja possui esse mesmo recorte natural).
    const QRectF contentClip = paintMapRect.intersected(mapRect);
    if (contentClip.isValid() && !contentClip.isEmpty()) {
        p.save();
        p.setClipRect(contentClip, Qt::IntersectClip);

        drawMapPanorama(p, info);

        const LayerPtr focusedLayer = ed.session.highlightCurrent ? ed.selectedLayer() : LayerPtr();

        RenderOptions opt;
        opt.animationTimeMs = ed.session.animateAutotiles ? m_animationClock.elapsed() : 0;
        opt.visualAnimationTimeMs = m_animationClock.elapsed();
        opt.previewParallax=true;
        opt.previewVisualEffects=true;
        opt.cameraOffset=m_pan;
        opt.focusDim = ed.session.focusDim;
        // As bordas azuis dos objetos pertencem ao contexto de foco da camada
        // de objetos e sao desenhadas em drawObjectDecorations(). Evita que
        // aparecam sobre Tile Layers ou sejam afetadas pelos passes de foco.
        opt.drawObjectFrames = false;
        opt.cacheMaskPaths = true;
        if (ed.session.tool == Tool::Object && ed.session.ghostPreview && m_hasHover && ed.currentStamp().valid()) {
            const auto layer=ed.activeLayer();
            if (layer && layer->type==LayerType::Object && !layerEffectiveState(ed.layers(),layer->id).locked) {
                const auto pos=mapToLayerPoint(layer,m_hoverMap);
                opt.previewLayerId=layer->id;
                opt.previewObject=paint::makeObjectFromStamp(ed,pos.x(),pos.y());
                opt.previewIndex=ed.session.objectInsertionIndex<0?int(layer->objects.size()):ed.session.objectInsertionIndex;
            }
        }

        // Focar camada respeita a ordem real da pilha:
        //   [camadas abaixo] -> veil preto -> [foco normal] -> [camadas acima translúcidas]
        // Assim uma camada acima do foco continua visível com alpha reduzido,
        // enquanto tudo que está abaixo vira o fundo escurecido do foco.
        if (focusedLayer) {
            const QVector<LayerPtr> renderables = flattenRenderableLayers(ed.layers());
            QHash<QString, int> renderOrder;
            renderOrder.reserve(renderables.size());
            int focusFirst = -1;
            int focusLast = -1;
            for (int i = 0; i < renderables.size(); ++i) {
                const LayerPtr& layer = renderables[i];
                if (!layer) continue;
                renderOrder.insert(layer->id, i);
                if (layerSubtreeContains(focusedLayer, layer->id)) {
                    if (focusFirst < 0) focusFirst = i;
                    focusLast = i;
                }
            }

            // Grupo vazio (ou seleção sem unidade renderizável): mantém o mapa
            // normal em vez de escurecer tudo sem existir um alvo visual.
            if (focusFirst < 0 || focusLast < focusFirst) {
                drawLayerTree(p, ed, ed.layers(), 1.0, opt);
            } else {
                RenderOptions belowOpt = opt;
                belowOpt.drawableFilter = [&renderOrder, focusFirst](const LayerPtr& layer) {
                    const auto it = renderOrder.constFind(layer->id);
                    return it != renderOrder.constEnd() && it.value() < focusFirst;
                };
                drawLayerTree(p, ed, ed.layers(), 1.0, belowOpt);

                // Apenas o que já foi desenhado (panorama + camadas abaixo) é
                // escurecido. focusDim representa quanto desse fundo permanece
                // visível: 0.25 = 25% do fundo + 75% de preto.
                const double backgroundVisibility = qBound(0.0, ed.session.focusDim, 1.0);
                QColor focusVeil(0, 0, 0);
                focusVeil.setAlphaF(1.0 - backgroundVisibility);
                p.save();
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                p.setOpacity(1.0);
                p.fillRect(contentClip, focusVeil);
                p.restore();

                RenderOptions focusOpt = opt;
                focusOpt.drawableFilter = [&renderOrder, focusFirst, focusLast](const LayerPtr& layer) {
                    const auto it = renderOrder.constFind(layer->id);
                    return it != renderOrder.constEnd() && it.value() >= focusFirst && it.value() <= focusLast;
                };
                drawLayerTree(p, ed, ed.layers(), 1.0, focusOpt);

                RenderOptions aboveOpt = opt;
                aboveOpt.drawableFilter = [&renderOrder, focusLast](const LayerPtr& layer) {
                    const auto it = renderOrder.constFind(layer->id);
                    return it != renderOrder.constEnd() && it.value() > focusLast;
                };
                // Camadas acima continuam na frente, mas transparentes.
                drawLayerTree(p, ed, ed.layers(), qBound(0.0, ed.session.focusDim, 1.0), aboveOpt);
            }
        } else {
            drawLayerTree(p, ed, ed.layers(), 1.0, opt);
        }

        // O LUDO Map Editor não desenha mais eventos, ponto inicial do jogador ou
        // sensores legados. Esses elementos pertencem ao RPG Maker MV/MZ e aparecem
        // sobre o panorama de referência depois da exportação.
        drawMarkers(p);
        core::drawGrid(p, ed, contentClip, m_zoom);
        drawObjectDecorations(p);
        drawTilePickPreview(p);
        p.restore();
    }

    // PREVIEW OVERSCAN: o conteudo persistente continua recortado ao mapa,
    // mas o ghost/footprint e desenhado no workspace inteiro. Assim um Pattern
    // 10x10 pode ter origem em (-4,-3), aparecer completo para posicionamento
    // e gravar somente a intersecao que realmente pertence ao mapa.
    if (!ed.session.regionMarkMode) {
        drawShapePreview(p);
        drawGhost(p);
        drawImageTransformControls(p);
        const LayerPtr hoverLayer = ed.activeLayer();
        const bool rasterHoverTarget = hoverLayer &&
            (isEditingRasterMask(ed, hoverLayer) ||
             (hoverLayer->type == LayerType::Image &&
              (hoverLayer->imagePaintLayer || hoverLayer->alphaLock)));
        // O quadrado de célula pertence às ferramentas de Tile/Região. Em
        // Paint/Mask ele competia visualmente com a silhueta real do pincel e
        // dava a impressão de que o brush ainda estava preso à grade.
        if (m_hasHover && !rasterHoverTarget) {
            const QRectF hoverRect = hoverVisualMapRect(m_hoverCell, m_hoverMap);
            if (hoverRect.isValid() && !hoverRect.isEmpty() && paintMapRect.intersects(hoverRect)) {
                p.setBrush(Qt::NoBrush);
                QPen hoverPen(mapRect.intersects(hoverRect)
                                  ? QColor(255,255,255,230)
                                  : QColor(150,150,150,170));
                hoverPen.setWidth(0); // 1 px de tela em qualquer zoom
                p.setPen(hoverPen);
                p.drawRect(hoverRect.adjusted(0.5 / m_zoom, 0.5 / m_zoom,
                                             -0.5 / m_zoom, -0.5 / m_zoom));
            }
        }
    }
    p.restore();

    if (m_marquee) {
        const QRectF r = QRectF(mapToScreen(m_marqueeStart), mapToScreen(m_marqueeEnd)).normalized();
        if (!event || event->rect().intersects(r.toAlignedRect().adjusted(-2,-2,2,2))) {
            p.setPen(QPen(QColor("#4a90d7"), 1, Qt::DashLine));
            p.setBrush(QColor(74, 144, 215, 40));
            p.drawRect(r);
        }
    }
    if (!m_editCellSelection.isEmpty() || !m_editRasterSelection.isEmpty()) {
        QRectF mapSelection;
        if (ed.session.regionMarkMode && m_editSelectionLayerId == QLatin1String("__regions__")) {
            const MapDoc* doc = ed.doc();
            if (doc) {
                const QRect cells = m_editCellSelection.intersected(QRect(0, 0, doc->map.width, doc->map.height));
                mapSelection = QRectF(cells.x() * doc->map.tileWidth,
                                      cells.y() * doc->map.tileHeight,
                                      cells.width() * doc->map.tileWidth,
                                      cells.height() * doc->map.tileHeight);
            }
        } else if (const LayerPtr selectedLayer = ed.activeLayer(); selectedLayer &&
                   selectedLayer->type == LayerType::Tile && m_editSelectionLayerId == selectedLayer->id) {
            const QRect cells = m_editCellSelection.intersected(QRect(0, 0, selectedLayer->cols, selectedLayer->rows));
            mapSelection = QRectF(cells.x() * selectedLayer->tileWidth + selectedLayer->offsetx,
                                  cells.y() * selectedLayer->tileHeight + selectedLayer->offsety,
                                  cells.width() * selectedLayer->tileWidth,
                                  cells.height() * selectedLayer->tileHeight);
        } else if (const LayerPtr selectedLayer = ed.activeLayer(); selectedLayer &&
                   m_editSelectionLayerId == selectedLayer->id && !m_editRasterSelection.isEmpty()) {
            mapSelection = rasterLocalRectToMapRect(selectedLayer, QRectF(m_editRasterSelection));
        }
        if (mapSelection.isValid() && !mapSelection.isEmpty()) {
            QRectF r(mapToScreen(mapSelection.topLeft()), mapToScreen(mapSelection.bottomRight()));
            r = r.normalized();
            p.setPen(QPen(QColor("#5eb6ff"), 1, Qt::DashLine));
            p.setBrush(QColor(94, 182, 255, 34));
            p.drawRect(r);
        }
    }

    if (m_slopeSelecting) {
        QRectF mapSelection(m_slopeStartMap, m_slopeEndMap);
        mapSelection = mapSelection.normalized();
        if (const LayerPtr slopeLayer = ed.activeLayer(); slopeLayer && slopeLayer->type == LayerType::Tile) {
            const QPoint a = cellAt(m_slopeStartMap, slopeLayer);
            const QPoint b = cellAt(m_slopeEndMap, slopeLayer);
            const QRect cells = normalizedCellRect(a, b).intersected(QRect(0, 0, slopeLayer->cols, slopeLayer->rows));
            if (!cells.isEmpty()) {
                mapSelection = QRectF(cells.x() * slopeLayer->tileWidth + slopeLayer->offsetx,
                                      cells.y() * slopeLayer->tileHeight + slopeLayer->offsety,
                                      cells.width() * slopeLayer->tileWidth,
                                      cells.height() * slopeLayer->tileHeight);
            }
        }
        QRectF r(mapToScreen(mapSelection.topLeft()), mapToScreen(mapSelection.bottomRight()));
        r = r.normalized();
        if (r.width() < 2.0) r.setWidth(2.0);
        if (r.height() < 2.0) r.setHeight(2.0);
        if (!event || event->rect().intersects(r.toAlignedRect().adjusted(-3,-3,3,3))) {
            p.setPen(QPen(QColor("#ffd45e"), 1, Qt::DashLine));
            p.setBrush(QColor(255, 212, 94, 36));
            p.drawRect(r);
        }
    }
}

int MapView::imageTransformHandleAt(const LayerPtr& layer,const QPointF& mapPos) const
{
    if(!layer||layer->type!=LayerType::Image||layer->image.isNull()||layer->imagePaintLayer||ed.session.tool!=Tool::Select)return 0;
    const QTransform t=imageLocalToMapTransform(layer);
    const QRectF source=imageVisualLocalRect(layer);
    const double radius=8.0/qMax(0.05,m_zoom);
    for(const QPointF& corner:{t.map(source.topLeft()),t.map(source.topRight()),t.map(source.bottomRight()),t.map(source.bottomLeft())})
        if(QLineF(corner,mapPos).length()<=radius)return 1;
    const QPointF top=t.map(QPointF(source.center().x(),source.top()));
    const QPointF center=t.map(source.center());
    QLineF axis(center,top);if(axis.length()<0.001)axis=QLineF(center,center+QPointF(0,-1));
    axis.setLength(axis.length()+28.0/qMax(0.05,m_zoom));
    if(QLineF(axis.p2(),mapPos).length()<=radius)return 2;
    return 0;
}

void MapView::drawImageTransformControls(QPainter& p)
{
    const LayerPtr layer=ed.activeLayer();
    if(!layer||layer->type!=LayerType::Image||layer->image.isNull()||layer->imagePaintLayer||ed.session.tool!=Tool::Select)return;
    const LayerEffectiveState state=layerEffectiveState(ed.layers(),layer->id);
    if(!state.visible)return;
    const QTransform t=imageLocalToMapTransform(layer);const QRectF source=imageVisualLocalRect(layer);
    QPolygonF polygon;polygon<<t.map(source.topLeft())<<t.map(source.topRight())<<t.map(source.bottomRight())<<t.map(source.bottomLeft());
    p.save();p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#5eb6ff"),0));p.drawPolygon(polygon);
    const double size=8.0/qMax(0.05,m_zoom);
    p.setBrush(QColor("#f2f2f2"));p.setPen(QPen(QColor("#2a78b8"),0));
    for(const QPointF& corner:polygon)p.drawRect(QRectF(corner-QPointF(size/2,size/2),QSizeF(size,size)));
    const QPointF top=t.map(QPointF(source.center().x(),source.top())),center=t.map(source.center());
    QLineF axis(center,top);if(axis.length()<0.001)axis=QLineF(center,center+QPointF(0,-1));axis.setLength(axis.length()+28.0/qMax(0.05,m_zoom));
    p.drawLine(top,axis.p2());p.setBrush(QColor("#5eb6ff"));p.drawEllipse(axis.p2(),size/2,size/2);p.restore();
}

void MapView::drawMarkers(QPainter& p)
{
    // Regiões são dados do MAPA (não do tileset) e usam sempre a grade base.
    if (ed.session.regionMarkMode) {
        const MapDoc* d = ed.doc();
        if (!d) return;
        const MapInfo& info = d->map;
        const QRectF vis = p.clipBoundingRect();
        const int tw = qMax(1, info.tileWidth), th = qMax(1, info.tileHeight);
        const int x0 = qMax(0, int(std::floor(vis.left() / tw)));
        const int y0 = qMax(0, int(std::floor(vis.top() / th)));
        const int x1 = qMin(info.width - 1, int(std::ceil(vis.right() / tw)));
        const int y1 = qMin(info.height - 1, int(std::ceil(vis.bottom() / th)));
        if (x1 < x0 || y1 < y0) return;

        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(qMax(7, int(qMin(tw, th) * 0.42)));
        p.setFont(f);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const int regionId = d->regionIdAt(x, y);
                if (regionId <= 0) continue;
                const QRectF cellRect(x * tw, y * th, tw, th);
                p.fillRect(cellRect, regionColor(regionId));
                QPen shadowPen(QColor(20, 20, 20, 210)); shadowPen.setWidth(0); p.setPen(shadowPen);
                p.drawText(cellRect.translated(0.8 / m_zoom, 0.8 / m_zoom), Qt::AlignCenter, QString::number(regionId));
                p.setPen(QColor(255, 255, 255, 245));
                p.drawText(cellRect, Qt::AlignCenter, QString::number(regionId));
            }
        }

        // Preview das ferramentas geométricas também usa a grade nativa de
        // regiões, então o que o usuário vê é exatamente o que irá para z=5.
        if (m_regionShapeActive) {
            QVector<QPoint> previewCells = semanticShapeCells(ed.session.tool, m_regionShapeStart,
                                                                     m_regionShapeEnd, !ed.session.shapeFilled);
            for (int i = previewCells.size()-1; i >= 0; --i)
                if (!d->regionInBounds(previewCells[i].x(),previewCells[i].y())) previewCells.removeAt(i);
            const bool areaGradient = !m_regionErase && ed.session.regionGradient && (ed.session.regionGradientPingPong || ed.session.regionGradientMerge);
            if(areaGradient) previewCells=mergedRegionCells(previewCells);
            const QVector<int> values = areaGradient ? regionAreaValues(previewCells) : QVector<int>();
            const QColor pc = m_regionErase ? QColor(255, 95, 95, 72)
                                             : regionColor(ed.session.activeRegionId, 72);
            QPen outline(m_regionErase ? QColor(255, 120, 120, 230) : QColor(255,255,255,190));
            outline.setWidth(0);
            p.setPen(outline);
            for (int i = 0; i < previewCells.size(); ++i) {
                const QPoint c = previewCells[i];
                if (!d->regionInBounds(c.x(), c.y())) continue;
                if (!m_regionErase && regionPaintValue(c) == 0) continue;
                const QRectF cellRect(c.x() * tw, c.y() * th, tw, th);
                p.fillRect(cellRect, m_regionErase ? pc : regionColor(areaGradient ? values[i] : regionPaintValue(c), 72));
                p.drawRect(cellRect.adjusted(0.5/m_zoom,0.5/m_zoom,-0.5/m_zoom,-0.5/m_zoom));
            }
        }

        if (!m_regionShapeActive && !m_regionPainting && m_hasHover &&
            ed.session.regionGradient && (ed.session.regionGradientPingPong || ed.session.regionGradientMerge) &&
            ed.session.activeRegionId > 0 && ed.session.tool == Tool::Stamp) {
            QVector<QPoint> cells;
            for (const QPoint& offset : paint::brushOffsets(ed.session.brush)) {
                const QPoint cell = m_hoverCell + offset;
                if (d->regionInBounds(cell.x(), cell.y())) cells.push_back(cell);
            }
            cells=mergedRegionCells(cells);
            const auto values = regionAreaValues(cells);
            for (int i = 0; i < cells.size(); ++i) {
                const QRectF dst(cells[i].x()*tw, cells[i].y()*th, tw, th);
                p.fillRect(dst, regionColor(values[i], 85));
                p.setPen(QColor(255,255,255,220));
                p.drawText(dst, Qt::AlignCenter, QString::number(values[i]));
            }
            return;
        }

        // Prévia da região ativa sob o cursor. Região 0 usa um X discreto para
        // deixar claro que o próximo clique irá apagar a marcação.
        if (!m_regionShapeActive && m_hasHover && d->regionInBounds(m_hoverCell.x(), m_hoverCell.y())) {
            const QRectF cellRect(m_hoverCell.x() * tw, m_hoverCell.y() * th, tw, th);
            if (ed.session.activeRegionId > 0) {
                p.fillRect(cellRect, regionColor(ed.session.activeRegionId, 55));
            } else {
                QPen erasePen(QColor(255, 110, 110, 220)); erasePen.setWidth(0); p.setPen(erasePen);
                p.drawLine(cellRect.topLeft(), cellRect.bottomRight());
                p.drawLine(cellRect.topRight(), cellRect.bottomLeft());
            }
        }
        return;
    }

    // Prioridade 0..5 e ✖ (colisão) são metadados do tile do tileset.
    if (!ed.session.starMarkMode && !ed.session.collisionMarkMode) return;
    const LayerPtr l = ed.activeLayer();
    if (!l || l->type != LayerType::Tile) return;
    const LayerEffectiveState state = layerEffectiveState(ed.layers(), l->id);
    if (!state.found || !state.visible) return;
    const QRectF vis = p.clipBoundingRect();
    const int tw = qMax(1, l->tileWidth), th = qMax(1, l->tileHeight);
    const int x0 = qMax(0, int(std::floor((vis.left() - l->offsetx) / tw)));
    const int y0 = qMax(0, int(std::floor((vis.top() - l->offsety) / th)));
    const int x1 = qMin(l->cols - 1, int(std::ceil((vis.right() - l->offsetx) / tw)));
    const int y1 = qMin(l->rows - 1, int(std::ceil((vis.bottom() - l->offsety) / th)));
    if (x1 < x0 || y1 < y0) return;
    QFont f = p.font();
    f.setPixelSize(qMax(6, int(l->tileHeight * 0.55)));
    p.setFont(f);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const QRectF cellRect(x * tw + l->offsetx, y * th + l->offsety, tw, th);
            const TileRef t = l->topAt(x, y);
            if (!t.isValid()) continue;
            if (ed.session.starMarkMode) {
                const int priority = ed.tilePriority(t.tilesetIdx, t.tx, t.ty);
                if (priority > 0) p.fillRect(cellRect, QColor(255,196,82,28 + priority*14));
                p.setPen(priority > 0 ? QColor("#fff0b0") : QColor(235,235,235,180));
                p.drawText(cellRect, Qt::AlignCenter, QString::number(priority));
            }
            if (ed.session.collisionMarkMode && ed.isCollisionMarked(t.tilesetIdx, t.tx, t.ty)) {
                const bool full = ed.isTileFullyBlocked(t.tilesetIdx, t.tx, t.ty);
                p.setPen(QColor("#ff6b6b"));
                p.drawText(cellRect, Qt::AlignCenter, full ? QStringLiteral("✖")
                                                           : QStringLiteral("◐"));
            }
        }
}

void MapView::drawGhost(QPainter& p)
{
    if (!ed.session.ghostPreview || !m_hasHover || m_painting || m_rasterPainting || m_tilePickDrag || m_rightEraseActive || ed.session.regionMarkMode) return;
    const LayerPtr l = ed.activeLayer();
    if (!l) return;
    const LayerEffectiveState state = layerEffectiveState(ed.layers(), l->id);
    if (!state.found || !state.visible || state.locked) return;

    if (ed.session.tool == Tool::Object || ed.session.tool == Tool::Slope) return;

    if (l->type == LayerType::Image && ed.session.tool == Tool::Select) {
        p.save();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#7fc7ff"), 0, Qt::DashLine));
        p.drawRect(imageLocalRectToMapRect(l, imageVisualLocalRect(l)));
        p.restore();
        return;
    }

    const bool rasterTarget = isEditingRasterMask(ed, l) ||
        (l->type == LayerType::Image && (l->imagePaintLayer || l->alphaLock));
    if (rasterTarget && (ed.session.tool == Tool::Paint || ed.session.tool == Tool::Eraser)) {
        const RasterBrushSettings& b = ed.session.rasterBrush;
        const bool erasePreview = ed.session.heldErase || ed.session.tool == Tool::Eraser;
        const QPointF local = l->type == LayerType::Image ? mapToImageLocalPoint(l, m_hoverMap)
                                                          : mapToLayerPoint(l, m_hoverMap);
        const QImage silhouette = rasterBrushSilhouette(
            b, erasePreview ? QColor("#ff6b6b") : QColor("#f2f2f2"), local);
        if (!silhouette.isNull()) {
            p.save();
            QPointF topLeft;
            if (b.pixelArt()) {
                topLeft = paint::rasterBrushTargetRect(b, local).topLeft();
            } else {
                topLeft = QPointF(local.x() - silhouette.width() / 2.0,
                                  local.y() - silhouette.height() / 2.0);
            }
            if (l->type == LayerType::Image) {
                p.setTransform(imageLocalToMapTransform(l), true);
                p.drawImage(topLeft, silhouette);
            } else {
                p.drawImage(topLeft + QPointF(l->offsetx, l->offsety), silhouette);
            }
            p.restore();
        }
        return;
    }

    if (l->type != LayerType::Tile) return;
    const int tw = l->tileWidth, th = l->tileHeight;
    const QPoint c = m_hoverCell;

    if (ed.session.tool == Tool::Eraser || ed.session.heldErase) {
        p.setPen(QPen(QColor("#ff6b6b"), 0));
        for (const paint::BrushSample& sample : paint::brushSamples(ed.session.brush)) {
            QColor fill(255, 107, 107,
                        qBound(12, int(std::lround(70.0 * sample.strength * (ed.session.brush.density / 100.0))), 70));
            p.setBrush(fill);
            const QPoint& o = sample.point;
            p.drawRect(QRectF((c.x() + o.x()) * tw + l->offsetx,
                              (c.y() + o.y()) * th + l->offsety, tw, th));
        }
        p.setBrush(Qt::NoBrush);
        return;
    }

    if (ed.session.tool == Tool::Terrain || semanticAutotileActive()) {
        const QRectF dst(c.x() * tw + l->offsetx, c.y() * th + l->offsety, tw, th);
        bool drewIcon = false;
        if (const WangSet* ws = ed.activeWangSet()) {
            if (const WangColor* terrain = ws->colorById(ed.session.activeWangColorId)) {
                int tsi = terrain->hasIcon ? terrain->iconTilesetIdx : ws->iconTilesetIdx;
                int tx = terrain->hasIcon ? terrain->iconTx : ws->iconTx;
                int ty = terrain->hasIcon ? terrain->iconTy : ws->iconTy;
                const bool hasIcon = terrain->hasIcon || ws->hasIcon;
                if (hasIcon) {
                    if (const Tileset* ts = ed.tilesetAt(tsi)) {
                        p.save();
                        p.setOpacity(0.65);
                        p.drawPixmap(dst, pixmapCache().pixmap(ed, tsi), QRectF(ts->tileRect(tx, ty)));
                        p.restore();
                        drewIcon = true;
                    }
                }
            }
        }
        p.setPen(QPen(QColor("#8fd18f"), 0));
        p.setBrush(drewIcon ? Qt::NoBrush : QBrush(QColor(120, 200, 120, 45)));
        p.drawRect(dst);
        p.setBrush(Qt::NoBrush);
        return;
    }

    const Stamp s = paint::transformStamp(ed.currentStamp(), ed.session.brush);
    if (!s.valid()) return;
    const QVector<paint::BrushSample> samples = paint::brushSamples(ed.session.brush);
    for (const paint::BrushSample& sample : samples) {
        const QPoint& bo = sample.point;
        p.setOpacity(qBound<qreal>(0.05, 0.55 * sample.strength * (ed.session.brush.density / 100.0), 0.55));
        for (int i = 0; i < s.tiles.size(); ++i) {
            const TileRef& t = s.tiles[i];
            const Tileset* ts = ed.tilesetAt(t.tilesetIdx);
            if (!ts) continue;
            const QPoint o = s.offsets[i];
            p.drawPixmap(QRectF((c.x() + bo.x() + o.x()) * tw + l->offsetx,
                                 (c.y() + bo.y() + o.y()) * th + l->offsety, tw, th),
                         pixmapCache().pixmap(ed, t.tilesetIdx),
                         QRectF(ts->tileRect(t.tx, t.ty)));
        }
    }
    p.setOpacity(1.0);
    p.setPen(QPen(QColor("#4a90d7"), 0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(c.x() * tw + l->offsetx, c.y() * th + l->offsety, s.w * tw, s.h * th));
}

void MapView::drawShapePreview(QPainter& p)
{
    const LayerPtr l = ed.activeLayer();
    if (!l || l->type != LayerType::Tile) return;
    const int tw = l->tileWidth, th = l->tileHeight;

    // Com a Borracha ativa, botão direito não pinta enquanto arrasta: ele
    // seleciona uma área retangular. O apagamento é atômico no mouseRelease.
    if (m_rightEraseActive) {
        const int x0 = qMin(m_rightEraseStart.x(), m_rightEraseLast.x());
        const int x1 = qMax(m_rightEraseStart.x(), m_rightEraseLast.x());
        const int y0 = qMin(m_rightEraseStart.y(), m_rightEraseLast.y());
        const int y1 = qMax(m_rightEraseStart.y(), m_rightEraseLast.y());
        const QRectF r(x0 * tw + l->offsetx, y0 * th + l->offsety,
                       (x1 - x0 + 1) * tw, (y1 - y0 + 1) * th);
        p.save();
        QColor fill(255, 80, 80, 48);
        p.fillRect(r, fill);
        QPen pen(QColor(255, 100, 100, 235));
        pen.setWidth(0);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.restore();
        return;
    }

    if (!m_shapeActive) return;
    const int x0 = qMin(m_shapeStart.x(), m_shapeEnd.x()), x1 = qMax(m_shapeStart.x(), m_shapeEnd.x());
    const int y0 = qMin(m_shapeStart.y(), m_shapeEnd.y()), y1 = qMax(m_shapeStart.y(), m_shapeEnd.y());
    const QRectF r(x0 * tw + l->offsetx, y0 * th + l->offsety,
                   (x1 - x0 + 1) * tw, (y1 - y0 + 1) * th);
    const QColor previewColor = m_erasing ? QColor("#ff6b6b") : QColor("#4a90d7");
    QPen previewPen(previewColor); previewPen.setWidth(0); previewPen.setStyle(Qt::DashLine);
    p.setPen(previewPen); QColor fill = previewColor; fill.setAlpha(40); p.setBrush(fill);
    if (ed.session.tool == Tool::Circle) p.drawEllipse(r);
    else if (ed.session.tool == Tool::Line) {
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF((m_shapeStart.x() + 0.5) * tw + l->offsetx,
                           (m_shapeStart.y() + 0.5) * th + l->offsety),
                   QPointF((m_shapeEnd.x() + 0.5) * tw + l->offsetx,
                           (m_shapeEnd.y() + 0.5) * th + l->offsety));
    } else p.drawRect(r);
    p.setBrush(Qt::NoBrush);
}

void MapView::drawTilePickPreview(QPainter& p)
{
    if (!m_tilePickDrag) return;
    const LayerPtr layer = ed.activeLayer();
    if (!layer || layer->type != LayerType::Tile) return;
    const int tw = qMax(1, layer->tileWidth), th = qMax(1, layer->tileHeight);
    const int x0 = qMin(m_tilePickStart.x(), m_tilePickEnd.x());
    const int x1 = qMax(m_tilePickStart.x(), m_tilePickEnd.x());
    const int y0 = qMin(m_tilePickStart.y(), m_tilePickEnd.y());
    const int y1 = qMax(m_tilePickStart.y(), m_tilePickEnd.y());
    const QRectF r(x0 * tw + layer->offsetx, y0 * th + layer->offsety,
                   (x1 - x0 + 1) * tw, (y1 - y0 + 1) * th);
    p.save();
    QColor fill(88, 170, 255, 42);
    p.fillRect(r, fill);
    p.setBrush(Qt::NoBrush);
    QPen pickPen(QColor(120, 205, 255, 235)); pickPen.setWidth(0); pickPen.setStyle(Qt::DashLine);
    p.setPen(pickPen);
    p.drawRect(r.adjusted(0.7 / m_zoom, 0.7 / m_zoom, -0.7 / m_zoom, -0.7 / m_zoom));
    p.restore();
}

void MapView::drawObjectDecorations(QPainter& p)
{
    const LayerPtr l = ed.activeLayer();
    if (!l || l->type != LayerType::Object) return;
    const LayerEffectiveState state = layerEffectiveState(ed.layers(), l->id);
    if (!state.found || !state.visible) return;

    const LayerPtr focused = ed.session.highlightCurrent ? ed.selectedLayer() : LayerPtr();
    const bool showObjectFrames = focused && focused->type == LayerType::Object && focused->id == l->id;
    const QRectF vis = p.clipBoundingRect();
    for (const MapObject& o : l->objects) {
        if (!o.visible) continue;
        const bool selected = ed.session.selectedObjectIds.contains(o.id) || ed.session.selectedObjectId == o.id;
        if (!showObjectFrames && !selected) continue;
        if (vis.isValid() && !vis.isNull() && !vis.intersects(objectMapRect(l, o))) continue;

        MapObject visual = o;
        visual.x += l->offsetx; visual.y += l->offsety;

        // Contorno azul editorial: somente quando a propria camada de objetos
        // e o alvo do modo Focar. Em qualquer outra camada o mapa fica limpo.
        if (showObjectFrames) {
            QPen framePen(QColor(74, 144, 215, 200));
            framePen.setWidth(0);
            p.setPen(framePen);
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(paint::objectPolygon(visual));
        }

        if (!selected) continue;
        QPen selectionPen(QColor("#ffd45e")); selectionPen.setWidth(0); selectionPen.setStyle(Qt::DashLine);
        p.setPen(selectionPen);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(paint::objectPolygon(visual));
        p.drawLine(paint::objectRotationStem(visual, m_zoom));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ffd45e"));
        for (const QRectF& h : paint::objectHandles(visual, m_zoom)) p.drawRect(h);
        p.setBrush(QColor("#1f2430"));
        p.setPen(QPen(QColor("#ffd45e"), 0));
        p.drawEllipse(paint::objectRotationHandle(visual, m_zoom));
        p.setBrush(Qt::NoBrush);
    }
}

// --------------------------------------------------------- interação do mapa
QVector<QPoint> MapView::mergedRegionCells(const QVector<QPoint>& cells) const
{
    if (!ed.session.regionGradientMerge || !ed.session.regionGradient) return cells;
    const MapDoc* d=ed.doc();
    if (!d) return cells;
    std::vector<std::pair<int,int>> source;
    for(const auto& cell:cells) source.emplace_back(cell.x(),cell.y());
    const auto merged=core::mergedRegionArea(source,ed.session.activeRegionId,ed.session.regionGradientEnd,
        [d](int x,int y){return d->regionInBounds(x,y)?d->regionIdAt(x,y):0;});
    QVector<QPoint> result;
    for(const auto& cell:merged) result.push_back(QPoint(cell.first,cell.second));
    return result;
}

QVector<int> MapView::regionAreaValues(const QVector<QPoint>& cells) const
{
    std::vector<std::pair<int,int>> area;
    area.reserve(cells.size());
    for (const QPoint& cell : cells) area.emplace_back(cell.x(),cell.y());
    auto values = core::regionAreaGradient(area, ed.session.activeRegionId, ed.session.regionGradientEnd);
    if (!ed.session.regionGradientPingPong && !cells.isEmpty()) {
        int origin=ed.session.regionGradientVertical?cells.first().y():cells.first().x();
        for(const auto& cell:cells) origin=qMin(origin,ed.session.regionGradientVertical?cell.y():cell.x());
        for(int i=0;i<cells.size();++i) values[i]=core::regionGradientValue(ed.session.activeRegionId,ed.session.regionGradientEnd,
            (ed.session.regionGradientVertical?cells[i].y():cells[i].x())-origin,false);
    }
    QVector<int> result;
    result.reserve(cells.size());
    for (int value : values) result.push_back(value);
    return result;
}

void MapView::updateRegionAreaGradient()
{
    if (m_regionErase || !ed.session.regionGradient || (!ed.session.regionGradientPingPong && !ed.session.regionGradientMerge)) return;
    MapDoc* d = ed.doc();
    if (!d) return;
    QVector<QPoint> cells;
    cells.reserve(m_regionBeforeValues.size());
    for (auto it = m_regionBeforeValues.cbegin(); it != m_regionBeforeValues.cend(); ++it)
        cells.push_back(QPoint(MapDoc::regionX(it.key()), MapDoc::regionY(it.key())));
    cells=mergedRegionCells(cells);
    const auto values = regionAreaValues(cells);
    for (int i = 0; i < cells.size(); ++i) {
        const auto& cell=cells[i];
        const quint64 key=MapDoc::regionKey(cell.x(),cell.y());
        if(!m_regionBeforeValues.contains(key)) m_regionBeforeValues.insert(key,d->regionIdAt(cell.x(),cell.y()));
        d->setRegionIdAt(cell.x(),cell.y(),values[i]);
    }
    if(ed.session.regionGradientMerge) update();
}

int MapView::regionPaintValue(const QPoint& cell) const
{
    if (!ed.session.regionGradient || ed.session.activeRegionId == 0) return ed.session.activeRegionId;
    if (ed.session.regionGradientPingPong) return ed.session.activeRegionId; // Recomputed from the complete stroke mask.
    const int distance = ed.session.regionGradientVertical ? qAbs(cell.y() - m_regionOrigin.y())
                                                          : qAbs(cell.x() - m_regionOrigin.x());
    return core::regionGradientValue(ed.session.activeRegionId, ed.session.regionGradientEnd,
                                     distance, ed.session.regionGradientPingPong);
}

void MapView::paintRegionCell(const QPoint& cell, bool erase)
{
    MapDoc* d = ed.doc();
    if (!d || !d->regionInBounds(cell.x(), cell.y())) return;
    const int value = regionPaintValue(cell);
    const quint64 key = MapDoc::regionKey(cell.x(), cell.y());
    if (!m_regionBeforeValues.contains(key))
        m_regionBeforeValues.insert(key, d->regionIdAt(cell.x(), cell.y()));
    d->setRegionIdAt(cell.x(), cell.y(), erase ? 0 : value);
}


void MapView::paintRegionBrush(const QPoint& cell, bool erase)
{
    for (const QPoint& o : paint::brushOffsets(ed.session.brush))
        paintRegionCell(cell + o, erase);
}

void MapView::fillRegionAt(const QPoint& cell, bool erase)
{
    MapDoc* d = ed.doc();
    if (!d || !d->regionInBounds(cell.x(), cell.y())) return;
    const int target = d->regionIdAt(cell.x(), cell.y());
    const int replacement = erase ? 0 : ed.session.activeRegionId;
    if (target == replacement && !ed.session.regionGradient) return;
    QStack<QPoint> stack;
    QSet<quint64> visited;
    stack.push(cell);
    while (!stack.isEmpty()) {
        const QPoint p = stack.pop();
        if (!d->regionInBounds(p.x(), p.y())) continue;
        const quint64 key = MapDoc::regionKey(p.x(), p.y());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (d->regionIdAt(p.x(), p.y()) != target) continue;
        paintRegionCell(p, erase);
        stack.push(QPoint(p.x()+1,p.y())); stack.push(QPoint(p.x()-1,p.y()));
        stack.push(QPoint(p.x(),p.y()+1)); stack.push(QPoint(p.x(),p.y()-1));
    }
}

LayerPtr MapView::ensureGridFreeScatterLayer()
{
    LayerPtr active = ed.activeLayer();
    if (active && active->type == LayerType::Object) return active;
    for (const LayerPtr& layer : ed.flatLayers()) {
        if (layer && layer->type == LayerType::Object && layer->name == tr("Aleatório Sem grade")) {
            ed.setSelectedLayerById(layer->id);
            return layer;
        }
    }
    LayerPtr created = makeObjectLayer(tr("Aleatório Sem grade"));
    ed.addLayer(created);
    emit statusMessage(tr("Camada de objetos 'Aleatório Sem grade' criada para o Dispersão livre."));
    return created;
}

void MapView::placeGridFreeAt(const LayerPtr& layer, const QPointF& mapPos)
{
    if (!layer || layer->type != LayerType::Object || ed.randomPool.isEmpty()) return;
    const QPointF local = mapToLayerPoint(layer, mapPos);
    bool valid = false;
    MapObject object = paint::makeRandomObjectFromPool(ed, local.x(), local.y(), &valid);
    if (!valid || object.tiles.isEmpty()) return;
    const double maxX = qMax(0.0, double(ed.mapInfo().pixelWidth()) - object.w);
    const double maxY = qMax(0.0, double(ed.mapInfo().pixelHeight()) - object.h);
    object.x = qBound(0.0, object.x, maxX);
    object.y = qBound(0.0, object.y, maxY);
    layer->objects.push_back(object);
}

void MapView::mousePressEvent(QMouseEvent* e)
{
    updateHeldModifiers(e->modifiers());
    setFocus();
    const QPointF mp = screenToMap(e->position());

    // Pan: botao do meio ou espaco.
    if (e->button() == Qt::MiddleButton ||
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier) && ed.session.tool == Tool::Select)) {
        m_panning = true;
        m_panStart = e->pos();
        m_panOrigin = m_pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // Seleção retangular unificada. Em Tile/Região/Paint/Mask a ferramenta
    // Seleção marca uma área real que os comandos globais podem copiar,
    // recortar, colar e apagar. Shift+arraste continua reservado ao pan.
    if (e->button() == Qt::LeftButton && ed.session.tool == Tool::Select &&
        !(e->modifiers() & Qt::ShiftModifier)) {
        const LayerPtr layer = ed.activeLayer();
        LayerPtr rasterLayer; QImage* rasterImage = nullptr;
        const bool selectable = ed.session.regionMarkMode ||
                                (layer && layer->type == LayerType::Tile) ||
                                rasterEditContext(&rasterLayer, &rasterImage, nullptr);
        if (selectable) { beginEditSelection(mp); return; }
    }

    // Botão direito é contextual:
    //  * com a BORRACHA ativa: arrasta uma área retangular e apaga tudo ao soltar;
    //  * nas demais ferramentas: botão direito + arraste captura uma área para Pattern;
    //  * clique direito simples continua sendo o picker do tile colocado.
    // A borracha tem prioridade inclusive sobre Shift: não existe ambiguidade entre
    // "capturar Pattern" e "apagar área" enquanto Tool::Eraser estiver ativo.
    if (e->button() == Qt::RightButton) {
        if (ed.session.regionMarkMode) {
            const QPoint cell = regionCellAt(mp);
            const MapDoc* d = ed.doc();
            if (d && d->regionInBounds(cell.x(), cell.y())) {
                ed.session.activeRegionId = d->regionIdAt(cell.x(), cell.y());
                ed.session.authoringContext = AuthoringContext::Region;
                emit ed.selectionChanged();
                const QString engineShort = core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper();
                emit statusMessage(ed.session.activeRegionId == 0
                    ? tr("Região %1 0 selecionada (apagar).").arg(engineShort)
                    : tr("Região %1 %2 copiada da célula %3,%4.")
                          .arg(engineShort).arg(ed.session.activeRegionId).arg(cell.x()).arg(cell.y()));
                updateMapRect(QRectF(cell.x() * ed.mapInfo().tileWidth, cell.y() * ed.mapInfo().tileHeight,
                                     ed.mapInfo().tileWidth, ed.mapInfo().tileHeight), 4);
            }
            return;
        }
        const LayerPtr layer = ed.activeLayer();
        if (layer && layer->type == LayerType::Tile) {
            const QPoint cell = cellAt(mp, layer);
            if (ed.session.tool == Tool::Eraser) {
                const LayerEffectiveState st = layerEffectiveState(ed.layers(), layer->id);
                if (!st.visible || st.locked) {
                    emit statusMessage(st.locked
                        ? tr("Camada bloqueada — não é possível apagar a área.")
                        : tr("Camada oculta — não é possível apagar a área."));
                    return;
                }
                m_rightEraseCandidate = true;
                m_rightEraseActive = true;
                m_rightEraseStart = m_rightEraseLast = cell;
                updateMapRect(tileCellMapRect(layer, cell), 5);
            } else {
                m_tilePickDrag = true;
                m_tilePickStart = m_tilePickEnd = cell;
                updateMapRect(tileCellMapRect(layer, cell), 5);
            }
        }
        return;
    }
    if (!ed.session.regionMarkMode && e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)) {
        pickPlacedTileAt(mp, e->modifiers() & Qt::ShiftModifier);
        return;
    }
    beginStroke(mp, e->button(), e->modifiers());
}

bool MapView::pickPlacedTileAt(const QPointF& mapPos, bool exactVariant)
{
    const LayerPtr layer = ed.activeLayer();
    if (!layer || layer->type != LayerType::Tile) return false;

    const QPoint cell = cellAt(mapPos, layer);
    const TileRef placed = layer->topAt(cell.x(), cell.y());
    if (!placed.isValid()) {
        emit statusMessage(tr("⚠ Nenhum tile colocado nesta célula para usar."));
        return false;
    }

    // Shift = copiar a variante visual exata, como o Shift Mapping do RPG Maker.
    // Não ativa a definição de Autotile/Wang; o próximo desenho usa este tile
    // como um tile comum e, portanto, não se realinha com os vizinhos.
    if (exactVariant) {
        const Tileset* ts = ed.tilesetAt(placed.tilesetIdx);
        if (!ts || !ts->contains(placed.tx, placed.ty)) return false;
        ed.session.authoringContext = AuthoringContext::Tileset;
        ed.session.activeAutotileId.clear();
        TileRef exact = placed;
        exact.wangSetId.clear();
        exact.wangColorId = -1;
        CustomStamp stamp; stamp.w = 1; stamp.h = 1;
        stamp.tiles.push_back(exact); stamp.offsets.push_back(QPoint(0, 0));
        ed.session.customStamp = stamp;
        ed.session.tool = Tool::Stamp;
        ed.session.wangBrushActive = false;
        emit ed.selectionChanged();
        emit statusMessage(tr("Variante visual copiada com Shift — será desenhada como tile comum, sem realinhar."));
        update();
        return true;
    }

    TilesetReverseSelection selection;
    if (!selectPlacedTileInPalette(ed, placed, &selection)) {
        emit statusMessage(tr("⚠ Não foi possível selecionar este tile."));
        return false;
    }

    if (selection.isAutotile()) {
        const TilesetAutotile* autotile =
            tilesetAutotileById(ed, selection.tilesetIdx, selection.autotileId);
        emit statusMessage(tr("Autotile “%1” selecionado.")
                               .arg(autotile ? autotile->name : tr("Autotile")));
    } else {
        emit statusMessage(tr("Tile (%1, %2) selecionado.")
                               .arg(selection.tile.x()).arg(selection.tile.y()));
    }
    update();
    return true;
}

void MapView::beginStroke(const QPointF& mapPos, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{

    // Regiões são uma camada lógica do MapXXX, não dependem da camada visual
    // atualmente selecionada e portanto funcionam até sobre Image/Object layers.
    if (ed.session.regionMarkMode) {
        if (button != Qt::LeftButton) return;
        MapDoc* d = ed.doc();
        const QPoint c = regionCellAt(mapPos);
        if (!d || !d->regionInBounds(c.x(), c.y())) return;
        m_regionOrigin = c;
        m_regionPainting = true;
        m_regionErase = (mods & Qt::ControlModifier) || ed.session.activeRegionId == 0 || ed.session.tool == Tool::Eraser;
        m_regionLastCell = c;
        m_regionBeforeValues.clear();
        m_regionBeforeAuthored = d->rpgMakerRegionsAuthored;
        m_regionShapeActive = false;
        ed.session.authoringContext = AuthoringContext::Region;
        if (ed.session.tool == Tool::Rect || ed.session.tool == Tool::Circle || ed.session.tool == Tool::Line) {
            m_regionShapeActive = true;
            m_regionShapeStart = m_regionShapeEnd = c;
        } else if (ed.session.tool == Tool::Fill) {
            fillRegionAt(c, m_regionErase);
        } else {
            paintRegionBrush(c, m_regionErase);
        }
        updateRegionAreaGradient();
        updateMapRect(QRectF(c.x() * d->map.tileWidth, c.y() * d->map.tileHeight,
                             d->map.tileWidth, d->map.tileHeight), 4);
        return;
    }

    // Grid-Free faz parte do Random Tile, não da ferramenta Object. Quando
    // ligado, o LUDO direciona o scatter para uma Object Layer dedicada e
    // mantém offsets sub-grid no mapa final.
    if (button == Qt::LeftButton && ed.session.tool == Tool::Stamp && ed.session.randomMode && ed.session.randomGridFree) {
        if (ed.randomPool.isEmpty()) { emit statusMessage(tr("Escolha os tiles aleatórios antes de pintar sem grade.")); return; }
        const QString previousLayerId = ed.activeLayer() ? ed.activeLayer()->id : QString();
        LayerPtr scatterLayer = ensureGridFreeScatterLayer();
        if (!scatterLayer) return;
        if (!previousLayerId.isEmpty() && previousLayerId != scatterLayer->id && ed.findNode(previousLayerId))
            ed.setSelectedLayerById(previousLayerId);
        const LayerEffectiveState scatterState = layerEffectiveState(ed.layers(), scatterLayer->id);
        if (!scatterState.visible || scatterState.locked) {
            emit statusMessage(tr("A camada Aleatório Sem grade está oculta ou bloqueada.")); return;
        }
        m_gridFreePainting = true;
        m_gridFreeLayerId = scatterLayer->id;
        m_gridFreeLastPoint = mapPos;
        m_gridFreeSession = ed.beginLayerEdit(scatterLayer);
        placeGridFreeAt(scatterLayer, mapPos);
        update();
        return;
    }

    const LayerPtr l = ed.activeLayer();
    if (!l) { emit statusMessage(tr("⚠ Nenhuma camada ativa.")); return; }
    const LayerEffectiveState state = layerEffectiveState(ed.layers(), l->id);
    if (state.locked) {
        emit statusMessage(tr("Camada bloqueada — a própria camada ou um grupo pai está bloqueado."));
        return;
    }
    if (!state.visible) {
        emit statusMessage(tr("Camada oculta — a própria camada ou um grupo pai está oculto."));
        return;
    }

    // Image Layer (inclusive o resultado de Slope) pode ser movida diretamente
    // com a ferramenta Seleção. O arraste altera apenas Offset X/Y e é uma única
    // operação de Undo/Redo.
    if (l->type == LayerType::Image && ed.session.tool == Tool::Select) {
        if (button != Qt::LeftButton) return;
        const int transformHandle=imageTransformHandleAt(l,mapPos);
        if(transformHandle){
            m_imageTransformHandle=transformHandle;m_imageDragLayerId=l->id;m_imageDragSession=ed.beginLayerEdit(l);
            m_imageTransformStartScaleX=l->imageScaleX;m_imageTransformStartScaleY=l->imageScaleY;
            m_imageTransformStartRotation=l->imageRotation;
            m_imageTransformCenter=imageLocalToMapTransform(l).map(imageVisualLocalRect(l).center());
            const double pointerAngle=QLineF(m_imageTransformCenter,mapPos).angle();
            m_imageRotationPointerOffset=l->imageRotation+pointerAngle;
            emit statusMessage(transformHandle==1?tr("Redimensionando imagem — Shift mantém a proporção."):tr("Girando imagem — solte para confirmar; Ctrl+Z desfaz."));
            return;
        }
        const QPointF local = mapToImageLocalPoint(l, mapPos);
        if (!imageVisualLocalRect(l).contains(local)) return;
        m_imageDragging = true;
        m_imageDragLayerId = l->id;
        m_imageDragStartMap = mapPos;
        m_imageDragStartOffset = QPoint(l->offsetx, l->offsety);
        m_imageDragStartOffsets.clear();
        for (const LayerPtr& selected : ed.selectedLayers()) {
            if (!selected || selected->type == LayerType::Group) continue;
            const LayerEffectiveState selectedState = layerEffectiveState(ed.layers(), selected->id);
            if (!selectedState.locked && selectedState.visible)
                m_imageDragStartOffsets.insert(selected->id, QPoint(selected->offsetx, selected->offsety));
        }
        if (!m_imageDragStartOffsets.contains(l->id))
            m_imageDragStartOffsets.insert(l->id, m_imageDragStartOffset);
        m_imageGroupDragBefore = ed.snapshotDoc();
        m_imageDragSession.valid = false;
        const int movingCount = m_imageDragStartOffsets.size();
        emit statusMessage(ed.session.snapObjects
            ? tr("Movendo %1 com encaixe de %2 px — solte para confirmar.")
                  .arg(movingCount == 1 ? tr("uma camada") : tr("%1 camadas").arg(movingCount))
                  .arg(ed.session.snapGridSize)
            : tr("Movendo %1 — solte para confirmar; Ctrl+Z desfaz.")
                  .arg(movingCount == 1 ? tr("uma camada") : tr("%1 camadas").arg(movingCount)));
        return;
    }

    // Slope trabalha por selecao retangular. Tile Layers sao rasterizadas para
    // uma nova Image Layer; Image/Paint Layers alteram os pixels da propria
    // camada. O dialogo de Step aparece somente ao soltar o mouse.
    if (ed.session.tool == Tool::Slope) {
        if (button != Qt::LeftButton) return;
        if (l->type != LayerType::Tile && l->type != LayerType::Image) {
            emit statusMessage(tr("A Inclinação funciona em camadas de tiles, imagem ou pintura."));
            return;
        }
        m_slopeSelecting = true;
        m_slopeStartMap = m_slopeEndMap = mapPos;
        update();
        return;
    }

    // Paint Brush raster: edita Paint/Image Layer ou uma máscara raster de
    // Image/Tile Layer. Em Tile Layer, Alpha Lock limita o brush à silhueta dos tiles.
    const bool editingMask = isEditingRasterMask(ed, l);
    const bool imageContentPaint = l->type == LayerType::Image && (l->imagePaintLayer || l->alphaLock);
    if ((editingMask || imageContentPaint) && (ed.session.tool == Tool::Paint || ed.session.tool == Tool::Eraser)) {
        if (button != Qt::LeftButton) return;
        const QPointF rawLocal = l->type == LayerType::Image ? mapToImageLocalPoint(l, mapPos)
                                                             : mapToLayerPoint(l, mapPos);
        const QSize targetSize = editingMask ? rasterMaskSizeFor(l) : l->image.size();

        // Conta-gotas rápido integrado ao mesmo brush. Alt captura a cor de
        // pintura; Shift+Alt captura a cor-alvo do Color Replace.
        if (mods & Qt::AltModifier) {
            const QImage* source = editingMask ? &l->imageMask : &l->image;
            const QPoint sample(int(std::floor(rawLocal.x())), int(std::floor(rawLocal.y())));
            if (source && !source->isNull() && source->rect().contains(sample)) {
                const QRgb rgba = qUnpremultiply(source->pixel(sample));
                const QColor picked(qRed(rgba), qGreen(rgba), qBlue(rgba), qAlpha(rgba));
                if (mods & Qt::ShiftModifier) {
                    ed.session.rasterBrush.pixelReplaceColor = picked;
                    ed.session.rasterBrush.pixelReplaceEnabled = true;
                    emit statusMessage(tr("Cor-alvo do Pixel Art capturada: %1").arg(picked.name(QColor::HexArgb)));
                } else {
                    ed.session.rasterBrush.color = picked;
                    emit statusMessage(tr("Cor do pincel capturada: %1").arg(picked.name(QColor::HexArgb)));
                }
                emit ed.selectionChanged();
                update();
            }
            return;
        }

        const QPointF local = paint::rasterBrushSnapPoint(ed.session.rasterBrush, rawLocal);
        const double radius = paint::rasterBrushFootprintPx(ed.session.rasterBrush) * 0.5;
        const QRectF targetBounds(QPointF(0, 0), QSizeF(targetSize));
        if (!targetBounds.adjusted(-radius, -radius, radius, radius).contains(local)) return;

        m_rasterPainting = true;
        m_rasterErasing = (mods & Qt::ControlModifier) || ed.session.heldErase || ed.session.tool == Tool::Eraser;
        m_rasterLastLocal = local;
        m_rasterPixelPerfectAnchor = local;
        m_rasterPixelPerfectPending = QPointF();
        m_rasterPixelPerfectPendingValid = false;
        m_rasterSpacingCarry = 0.0;
        m_rasterUseClipRegion = false;
        m_rasterClipRegion = QRegion();
        m_rasterSession = ed.beginLayerEdit(l);
        QRectF dirtyLocal;

        if (editingMask) {
            if (l->imageMask.size() != targetSize) {
                QImage resized(targetSize, QImage::Format_ARGB32_Premultiplied);
                resized.fill(Qt::white);
                QPainter rp(&resized); rp.drawImage(QPoint(0, 0), l->imageMask); rp.end();
                l->imageMask = resized;
            }
            if (l->alphaLock) {
                m_rasterClipRegion = l->type == LayerType::Tile ? tileContentRegion(l)
                                                               : imageAlphaRegion(l);
                m_rasterUseClipRegion = true;
            }
            RasterBrushSettings maskBrush = ed.session.rasterBrush;
            maskBrush.blendMode = QStringLiteral("source-over");
            dirtyLocal = paint::rasterBrushDab(&l->imageMask, maskBrush, local,
                                               m_rasterErasing, 0.0, true, false,
                                               m_rasterUseClipRegion ? &m_rasterClipRegion : nullptr);
        } else {
            dirtyLocal = paint::rasterBrushDab(l, ed.session.rasterBrush, local, m_rasterErasing, 0.0);
        }
        if (!dirtyLocal.isEmpty()) {
            ed.markLayerEditRasterDirty(m_rasterSession, dirtyLocal.toAlignedRect(), editingMask);
            updateMapRect(rasterLocalRectToMapRect(l, dirtyLocal), 4);
        }
        return;
    }

    const QPoint c = cellAt(mapPos, l);

    // O workspace ao redor do mapa e navegavel E autoravel para stamps grandes.
    // A origem pode ficar fora (inclusive x/y negativos) desde que o footprint
    // visual toque o mapa. Para Tile Layers, putTile/putStack fazem clipping
    // celula-a-celula; para Object Layers, o objeto pode manter coordenadas
    // negativas e o renderer/export continuam recortando pelo retangulo do mapa.
    const QRectF mapBounds(0, 0, ed.mapInfo().pixelWidth(), ed.mapInfo().pixelHeight());
    const bool originInside = mapPos.x() >= 0.0 && mapPos.y() >= 0.0 &&
                              mapPos.x() < mapBounds.width() && mapPos.y() < mapBounds.height();
    if (!originInside) {
        const QRectF footprint = hoverVisualMapRect(c, mapPos);
        const bool stampOverscan = l->type == LayerType::Tile && (ed.session.tool == Tool::Stamp || ed.session.tool == Tool::Fill) &&
                                   footprint.isValid() && mapBounds.intersects(footprint);
        const bool objectOverscan = l->type == LayerType::Object && ed.session.tool == Tool::Object &&
                                    footprint.isValid() && mapBounds.intersects(footprint);
        const bool geometryOverscan = l->type == LayerType::Tile &&
            (ed.session.tool == Tool::Rect || ed.session.tool == Tool::Circle || ed.session.tool == Tool::Line || ed.session.tool == Tool::Eraser || ed.session.tool == Tool::Terrain);
        if (!stampOverscan && !objectOverscan && !geometryOverscan) return;
    }

    // Pintura principal acontece com o botão esquerdo. Ctrl também apaga;
    // arraste com o botão direito é tratado acima como borracha contínua.
    if (button != Qt::LeftButton) return;

    // ---- camada de objetos ----------------------------------------------
    if (l->type == LayerType::Object) {
        if (ed.session.authoringContext != AuthoringContext::Pattern) ed.session.authoringContext = AuthoringContext::Object;
        if (ed.session.tool == Tool::Object) {
            m_session = ed.beginLayerEdit(l);
            const QPointF localPos = mapToLayerPoint(l, mapPos);
            MapObject o = paint::makeObjectFromStamp(ed, localPos.x(), localPos.y());
            const int insertion = core::objectInsertionPosition(ed.session.objectInsertionIndex,int(l->objects.size()));
            l->objects.insert(insertion, o);
            ed.session.selectedObjectId = o.id;
            ed.session.selectedObjectIds = { o.id };
            ed.commitLayerEdit(m_session, tr("Colocar objeto"));
            emit ed.selectionChanged();
            update();
            return;
        }
        // ferramenta de selecao
        const QPointF localPos = mapToLayerPoint(l, mapPos);
        MapObject* hit = nullptr;
        int handle = -1;
        bool rotationHandle = false;

        // As alcas ficam parcialmente fora do poligono. Testa primeiro os
        // objetos ja selecionados, de frente para tras, antes do hit normal.
        for (int i = l->objects.size() - 1; i >= 0; --i) {
            MapObject& candidate = l->objects[i];
            if (!candidate.visible ||
                (!ed.session.selectedObjectIds.contains(candidate.id) && ed.session.selectedObjectId != candidate.id)) continue;
            if (paint::hitRotationHandle(candidate, localPos.x(), localPos.y(), m_zoom)) {
                hit = &candidate;
                rotationHandle = true;
                break;
            }
            const int candidateHandle = paint::hitHandle(candidate, localPos.x(), localPos.y(), m_zoom);
            if (candidateHandle >= 0) {
                hit = &candidate;
                handle = candidateHandle;
                break;
            }
        }
        if (!hit) hit = paint::hitObject(l, mapPos.x(), mapPos.y());
        if (hit) {
            if (handle < 0 && !rotationHandle)
                handle = paint::hitHandle(*hit, localPos.x(), localPos.y(), m_zoom);
            if (!(mods & Qt::ControlModifier)) {
                if (!ed.session.selectedObjectIds.contains(hit->id)) ed.session.selectedObjectIds = { hit->id };
            } else {
                if (ed.session.selectedObjectIds.contains(hit->id)) ed.session.selectedObjectIds.remove(hit->id);
                else ed.session.selectedObjectIds.insert(hit->id);
            }
            ed.session.selectedObjectId = hit->id;
            m_dragObjectId = hit->id;
            m_dragHandle = handle;
            m_rotatingObject = rotationHandle;
            m_dragOffset = QPointF(localPos.x() - hit->x, localPos.y() - hit->y);
            m_dragStartRect = hit->rect();
            m_dragStartRotation = hit->rotation;
            if (m_rotatingObject) {
                constexpr double kPi = 3.14159265358979323846;
                const QPointF center = hit->rect().center();
                const double pointerAngle = std::atan2(localPos.y() - center.y(),
                                                       localPos.x() - center.x()) * 180.0 / kPi;
                m_rotationGrabOffset = hit->rotation - pointerAngle;
            }
            m_session = ed.beginLayerEdit(l);
            emit ed.selectionChanged();
        } else {
            ed.session.selectedObjectIds.clear();
            ed.session.selectedObjectId.clear();
            m_marquee = true;
            m_marqueeStart = m_marqueeEnd = mapPos;
            emit ed.selectionChanged();
        }
        update();
        return;
    }

    if (l->type != LayerType::Tile) return;

    // ---- modos de marcacao (★ / ✖) --------------------------------------
    if (ed.session.starMarkMode || ed.session.collisionMarkMode) {
        const TileRef t = l->topAt(c.x(), c.y());
        if (t.isValid()) {
            if (ed.session.starMarkMode) ed.cycleTilePriority(t.tilesetIdx, t.tx, t.ty);
            else ed.toggleTileBlocked(t.tilesetIdx, t.tx, t.ty);
        }
        return;
    }

    m_painting = true;
    m_erasing = (mods & Qt::ControlModifier) || ed.session.tool == Tool::Eraser;
    m_preserveAutotileShape = m_erasing && (mods & Qt::ShiftModifier);
    if (m_preserveAutotileShape)
        emit statusMessage(tr("Apagando sem reorganizar Autotiles (Shift)."));
    m_lastCell = c;
    m_brushSpacingCounter = 0;
    m_randomScatterVisited.clear();
    m_session = ed.beginLayerEdit(l);

    bool fullDirty = false;
    const bool semantic = semanticAutotileActive();
    QSet<quint64>* scatterVisited =
        (ed.session.randomMode && ed.session.randomScattering && !ed.session.randomGridFree)
            ? &m_randomScatterVisited : nullptr;
    switch (ed.session.tool) {
    case Tool::Rect: case Tool::Circle: case Tool::Line:
        m_shapeActive = true;
        m_shapeStart = m_shapeEnd = c;
        break;
    case Tool::Fill:
        if (semantic) {
            applySemanticAutotileFill(l, QPoint(qBound(0,c.x(),l->cols-1),qBound(0,c.y(),l->rows-1)), m_erasing || ed.session.wangEraseMode);
        } else {
            if (m_erasing) paint::eraseAt(ed, l, c.x(), c.y());
            else paint::fillAt(ed, l, qBound(0,c.x(),l->cols-1), qBound(0,c.y(),l->rows-1));
            // Com Shift durante o apagamento, preserve exatamente as variantes
            // atuais dos Autotiles; caso contrário mantenha o comportamento normal.
            if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                wang::retileTerrainLayer(ed, l);
            invalidateMaskPathCache(l->id);
        }
        fullDirty = true;
        break;
    case Tool::Terrain:
    case Tool::Stamp:
    case Tool::Eraser:
        if (semantic) {
            QVector<QPoint> cells;
            for (const QPoint& o : paint::brushOffsets(ed.session.brush)) cells.push_back(c + o);
            applySemanticAutotileCells(l, cells, m_erasing || ed.session.wangEraseMode || ed.session.tool == Tool::Eraser);
        } else if (ed.session.tool == Tool::Terrain) {
            applyTerrainAt(l, c.x(), c.y(), m_erasing || ed.session.wangEraseMode);
            invalidateMaskPathCache(l->id);
        } else if (ed.session.tool == Tool::Eraser) {
            paint::brushStroke(ed, l, c.x(), c.y(), true);
            if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                wang::retileTerrainNeighborhood(ed, l, regularPaintFootprint(ed, c.x(), c.y()));
            invalidateMaskPathCache(l->id);
        } else {
            paint::brushStroke(ed, l, c.x(), c.y(), m_erasing, scatterVisited);
            if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                wang::retileTerrainNeighborhood(ed, l, regularPaintFootprint(ed, c.x(), c.y()));
            invalidateMaskPathCache(l->id);
        }
        break;
    default:
        paint::brushStroke(ed, l, c.x(), c.y(), m_erasing, scatterVisited);
        if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
            wang::retileTerrainNeighborhood(ed, l, regularPaintFootprint(ed, c.x(), c.y()));
        invalidateMaskPathCache(l->id);
        break;
    }
    if (fullDirty) update();
    else if (m_shapeActive) updateMapRect(shapePreviewMapRect(), 5);
    else updateMapRect(hoverVisualMapRect(c, mapPos), 4);
}

bool MapView::semanticAutotileActive() const
{
    if (ed.session.terrainManual || ed.session.activeAutotileId.isEmpty()) return false;
    int owner = -1;
    const TilesetAutotile* autotile = tilesetAutotileById(ed, ed.session.activeAutotileId, &owner);
    if (!autotile || owner < 0 || !autotile->hasTerrain()) return false;
    const WangSet* set = ed.activeWangSet();
    return set && set->id == autotile->wangSetId &&
           set->colorById(autotile->wangColorId) != nullptr;
}

wang::TerrainPaintContext MapView::activeAutotileContext() const
{
    wang::TerrainPaintContext context;
    context.sourceEditor = &ed;
    int owner = -1;
    if (const TilesetAutotile* autotile = tilesetAutotileById(ed, ed.session.activeAutotileId, &owner)) {
        if (owner >= 0) context = wang::contextForAutotile(ed, owner, autotile->id);
    }
    context.preserveCellStack = ed.session.placeOnTop || ed.session.heldStack;
    return context;
}

QVector<QPoint> MapView::semanticShapeCells(Tool tool, const QPoint& a, const QPoint& b, bool hollow) const
{
    QVector<QPoint> cells;
    const int minX = qMin(a.x(), b.x()), maxX = qMax(a.x(), b.x());
    const int minY = qMin(a.y(), b.y()), maxY = qMax(a.y(), b.y());
    if (tool == Tool::Line) {
        const QVector<QPoint> line = paint::linePoints(a.x(), a.y(), b.x(), b.y());
        const QVector<QPoint> brush = paint::brushOffsets(ed.session.brush);
        QSet<qint64> seen;
        for (int i = 0; i < line.size(); ++i) {
            if (i % qMax(1, ed.session.brush.spacing)) continue;
            for (const QPoint& o : brush) {
                const QPoint p = line.at(i) + o;
                const qint64 key = (qint64(p.y()) << 32) ^ quint32(p.x());
                if (!seen.contains(key)) { seen.insert(key); cells.push_back(p); }
            }
        }
        return cells;
    }
    if (tool == Tool::Circle) {
        const double cx = (minX + maxX) / 2.0, cy = (minY + maxY) / 2.0;
        double rx = (maxX - minX + 1) / 2.0, ry = (maxY - minY + 1) / 2.0;
        rx = qMax(0.5, rx); ry = qMax(0.5, ry);
        auto inside = [&](int x, int y) {
            const double dx = (x - cx) / rx, dy = (y - cy) / ry;
            return dx * dx + dy * dy <= 1.02;
        };
        for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
            if (!inside(x, y)) continue;
            if (hollow) {
                const bool n1 = x > minX && inside(x - 1, y);
                const bool n2 = x < maxX && inside(x + 1, y);
                const bool n3 = y > minY && inside(x, y - 1);
                const bool n4 = y < maxY && inside(x, y + 1);
                if (n1 && n2 && n3 && n4) continue;
            }
            cells.push_back(QPoint(x, y));
        }
        return cells;
    }
    for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
        if (hollow && x != minX && x != maxX && y != minY && y != maxY) continue;
        cells.push_back(QPoint(x, y));
    }
    return cells;
}

void MapView::applySemanticAutotileCells(const LayerPtr& layer, const QVector<QPoint>& cells, bool erase)
{
    if (erase && m_preserveAutotileShape) {
        for (const QPoint& cell : cells) paint::eraseAt(ed, layer, cell.x(), cell.y());
        invalidateMaskPathCache(layer->id);
        return;
    }
    WangSet* set = ed.activeWangSet();
    if (!set || !semanticAutotileActive()) {
        emit statusMessage(tr("Este Autotile ainda não tem regras de conexão configuradas. Abra o Gerenciador de Tilesets e configure como ele deve se conectar aos vizinhos."));
        return;
    }
    const wang::TerrainPaintContext context = activeAutotileContext();
    const int changed = wang::paintCells(ed, layer, cells, *set, ed.session.activeWangColorId, erase, context);
    if (changed > 0) invalidateMaskPathCache(layer->id);
}

void MapView::applySemanticAutotileFill(const LayerPtr& layer, const QPoint& start, bool erase)
{
    if (!layer || !layer->inBounds(start.x(), start.y()) || !semanticAutotileActive()) return;
    const TileRef seed = layer->topAt(start.x(), start.y());
    QString seedAutotileId;
    if (seed.isValid()) {
        if (const TilesetAutotile* a = tilesetAutotileAt(ed, seed.tilesetIdx, seed.tx, seed.ty))
            seedAutotileId = a->id;
    }
    auto sameTarget = [&](const TileRef& t) {
        if (!seed.isValid()) return !t.isValid();
        if (!t.isValid()) return false;
        if (!seedAutotileId.isEmpty()) {
            if (const TilesetAutotile* a = tilesetAutotileAt(ed, t.tilesetIdx, t.tx, t.ty))
                return a->id == seedAutotileId;
            return false;
        }
        if (!seed.wangSetId.isEmpty() && seed.wangColorId >= 0)
            return t.wangSetId == seed.wangSetId && t.wangColorId == seed.wangColorId;
        return t.key() == seed.key();
    };

    QVector<QPoint> cells;
    QStack<QPoint> stack;
    QSet<qint64> visited;
    stack.push(start);
    while (!stack.isEmpty()) {
        const QPoint p = stack.pop();
        if (!layer->inBounds(p.x(), p.y())) continue;
        const qint64 key = (qint64(p.y()) << 32) ^ quint32(p.x());
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (!sameTarget(layer->topAt(p.x(), p.y()))) continue;
        cells.push_back(p);
        stack.push(QPoint(p.x() + 1, p.y()));
        stack.push(QPoint(p.x() - 1, p.y()));
        stack.push(QPoint(p.x(), p.y() + 1));
        stack.push(QPoint(p.x(), p.y() - 1));
    }
    applySemanticAutotileCells(layer, cells, erase);
}

void MapView::applyTerrainAt(const LayerPtr& layer, int gx, int gy, bool erase)
{
    WangSet* set = ed.activeWangSet();
    if (!set) {
        emit statusMessage(tr("Nenhuma conexão automática está pronta para este Autotile. Configure-a em Gerenciador de Tilesets ▸ Conexões do Autotile."));
        return;
    }
    if (ed.session.terrainManual && !erase) {
        // Modo manual: pinta o tile selecionado sem autotile (correcoes finas).
        paint::stampAt(ed, layer, gx, gy);
        if (!(ed.session.placeOnTop || ed.session.heldStack))
            wang::retileTerrainNeighborhood(ed, layer, regularPaintFootprint(ed, gx, gy));
        invalidateMaskPathCache(layer->id);
        return;
    }
    wang::TerrainPaintContext topology;
    topology.sourceEditor = &ed;
    if (!ed.session.activeAutotileId.isEmpty()) {
        int autotileTilesetIdx = -1;
        if (tilesetAutotileById(ed, ed.session.activeAutotileId, &autotileTilesetIdx))
            topology = wang::contextForAutotile(ed, autotileTilesetIdx, ed.session.activeAutotileId);
    }
    topology.preserveCellStack = ed.session.placeOnTop || ed.session.heldStack;
    if (!wang::paintAt(layer, gx, gy, *set, ed.session.activeWangColorId, erase, topology))
        emit statusMessage(tr("Não encontrei uma variação deste Autotile para esta combinação de vizinhos. "
                              "Revise as conexões em Gerenciador de Tilesets ▸ Conexões do Autotile."));
    else
        invalidateMaskPathCache(layer->id);
}

void MapView::mouseMoveEvent(QMouseEvent* e)
{
    updateHeldModifiers(e->modifiers());
    // Não invalide o viewport inteiro a cada movimento do mouse. Cada modo de
    // interação abaixo já atualiza sua área suja (hover, pincel, objeto,
    // seleção, raster etc.). O repaint global aqui anulava essas otimizações e
    // ficava especialmente perceptível em mapas grandes/zoom baixo.
    const QPoint oldHoverCell = m_hoverCell;
    const QPointF oldHoverMap = m_hoverMap;
    const bool hadHover = m_hasHover;
    const QRectF oldHoverDirty = hadHover ? hoverVisualMapRect(oldHoverCell, oldHoverMap) : QRectF();

    const QPointF mp = screenToMap(e->position());
    const LayerPtr l = ed.activeLayer();
    m_hasHover = true;
    m_hoverMap = mp;
    if (ed.session.regionMarkMode) m_hoverCell = regionCellAt(mp);
    else m_hoverCell = cellAt(mp, l);
    emit cursorMoved(QPoint(int(mp.x()), int(mp.y())), m_hoverCell);
    const QRectF newHoverDirty = hoverVisualMapRect(m_hoverCell, m_hoverMap);

    if (m_editSelecting && (e->buttons() & Qt::LeftButton)) {
        updateEditSelection(mp);
        return;
    }

    if (m_slopeSelecting && (e->buttons() & Qt::LeftButton)) {
        m_slopeEndMap = mp;
        update();
        return;
    }

    if (m_rightEraseActive && (e->buttons() & Qt::RightButton)) {
        const LayerPtr eraseLayer = ed.activeLayer();
        if (eraseLayer && eraseLayer->type == LayerType::Tile) {
            const QPoint current = cellAt(mp, eraseLayer);
            if (current != m_rightEraseLast) {
                const QRect oldCells = normalizedCellRect(m_rightEraseStart, m_rightEraseLast);
                const QRect newCells = normalizedCellRect(m_rightEraseStart, current);
                m_rightEraseLast = current;
                const QRect dirty = oldCells.united(newCells);
                updateMapRect(QRectF(dirty.x() * eraseLayer->tileWidth + eraseLayer->offsetx,
                                     dirty.y() * eraseLayer->tileHeight + eraseLayer->offsety,
                                     dirty.width() * eraseLayer->tileWidth,
                                     dirty.height() * eraseLayer->tileHeight), 5);
            }
        }
        return;
    }

    if (m_gridFreePainting && (e->buttons() & Qt::LeftButton)) {
        LayerPtr scatter = ed.findNode(m_gridFreeLayerId);
        if (!scatter || scatter->type != LayerType::Object) { m_gridFreePainting = false; return; }
        const double spacing = qMax(1, ed.session.randomGridFreeSpacing);
        QLineF path(m_gridFreeLastPoint, mp);
        if (path.length() >= spacing) {
            const int samples = qMax(1, int(std::floor(path.length() / spacing)));
            for (int i = 1; i <= samples; ++i) {
                const double t = qMin(1.0, (i * spacing) / path.length());
                placeGridFreeAt(scatter, path.pointAt(t));
            }
            m_gridFreeLastPoint = mp;
            update();
        }
        return;
    }

    if (m_regionPainting) {
        MapDoc* d = ed.doc();
        if (!d) return;
        const QPoint current = regionCellAt(mp);
        if (m_regionShapeActive) {
            if (current != m_regionShapeEnd) {
                const QRect before = normalizedCellRect(m_regionShapeStart, m_regionShapeEnd);
                m_regionShapeEnd = current;
                const QRect after = normalizedCellRect(m_regionShapeStart, m_regionShapeEnd);
                const QRect dirty = before.united(after);
                if(ed.session.regionGradientMerge) update();
                updateMapRect(QRectF(dirty.x()*d->map.tileWidth, dirty.y()*d->map.tileHeight,
                                     dirty.width()*d->map.tileWidth, dirty.height()*d->map.tileHeight), 5);
            }
            return;
        }
        if (ed.session.tool == Tool::Fill) return;
        if (current != m_regionLastCell) {
            const QPoint from = m_regionLastCell;
            for (const QPoint& pt : paint::linePoints(from.x(), from.y(), current.x(), current.y()))
                paintRegionBrush(pt, m_regionErase);
            updateRegionAreaGradient();
            m_regionLastCell = current;
            const int tw = d->map.tileWidth, th = d->map.tileHeight;
            const QRect cells = normalizedCellRect(from, current);
            updateMapRect(QRectF(cells.x() * tw, cells.y() * th,
                                 cells.width() * tw, cells.height() * th), 4);
        }
        return;
    }

    if (m_tilePickDrag && l && l->type == LayerType::Tile) {
        const QPoint before = m_tilePickEnd;
        m_tilePickEnd = cellAt(mp, l);
        const QRect oldRect = normalizedCellRect(m_tilePickStart, before);
        const QRect newRect = normalizedCellRect(m_tilePickStart, m_tilePickEnd);
        const QRect united = oldRect.united(newRect);
        updateMapRect(QRectF(united.x() * l->tileWidth + l->offsetx,
                             united.y() * l->tileHeight + l->offsety,
                             united.width() * l->tileWidth, united.height() * l->tileHeight), 5);
        return;
    }

    if (m_panning) {
        const QPointF delta = QPointF(e->pos() - m_panStart) / m_zoom;
        m_pan = m_panOrigin - delta;
        clampPan();
        emit viewportChanged();
        update(); // a câmera mudou: toda a viewport possui conteúdo diferente.
        return;
    }

    if (m_marquee) {
        const QRectF before = QRectF(m_marqueeStart, m_marqueeEnd).normalized();
        m_marqueeEnd = mp;
        const QRectF after = QRectF(m_marqueeStart, m_marqueeEnd).normalized();
        updateMapRect(before.united(after), 5);
        return;
    }

    if (m_imageDragging && (e->buttons() & Qt::LeftButton)) {
        LayerPtr imageLayer = ed.findNode(m_imageDragLayerId);
        if (!imageLayer || imageLayer->type != LayerType::Image) {
            m_imageDragging = false;
            return;
        }
        const QRectF before = imageLocalRectToMapRect(imageLayer, imageVisualLocalRect(imageLayer));
        const QPointF delta = mp - m_imageDragStartMap;
        int nextX = m_imageDragStartOffset.x() + qRound(delta.x());
        int nextY = m_imageDragStartOffset.y() + qRound(delta.y());
        if (ed.session.snapObjects || ed.session.heldSnap) {
            const int grid = qMax(1, ed.session.snapGridSize);
            nextX = qRound(double(nextX) / grid) * grid;
            nextY = qRound(double(nextY) / grid) * grid;
        }
        const QPoint appliedDelta = QPoint(nextX, nextY) - m_imageDragStartOffset;
        for (auto it = m_imageDragStartOffsets.constBegin(); it != m_imageDragStartOffsets.constEnd(); ++it) {
            const LayerPtr selected = ed.findNode(it.key());
            if (!selected || selected->type == LayerType::Group) continue;
            selected->offsetx = it.value().x() + appliedDelta.x();
            selected->offsety = it.value().y() + appliedDelta.y();
        }
        const QRectF after = imageLocalRectToMapRect(imageLayer, imageVisualLocalRect(imageLayer));
        if (m_imageDragStartOffsets.size() > 1) update();
        else updateMapRect(before.united(after), 6);
        return;
    }
    if(m_imageTransformHandle&&(e->buttons()&Qt::LeftButton)){
        LayerPtr imageLayer=ed.findNode(m_imageDragLayerId);if(!imageLayer){m_imageTransformHandle=0;return;}
        if(m_imageTransformHandle==2){
            imageLayer->imageRotation=m_imageRotationPointerOffset-QLineF(m_imageTransformCenter,mp).angle();
        }else{
            QTransform unrotate;unrotate.rotate(-m_imageTransformStartRotation);
            const QPointF v=unrotate.map(mp-m_imageTransformCenter);
            const QSize frameSize=core::visualLayerFrameSize(imageLayer);
            double sx=qBound(0.01,qAbs(v.x())*2.0/qMax(1,frameSize.width()),100.0);
            double sy=qBound(0.01,qAbs(v.y())*2.0/qMax(1,frameSize.height()),100.0);
            if(e->modifiers()&Qt::ShiftModifier){const double ratio=qMax(sx/m_imageTransformStartScaleX,sy/m_imageTransformStartScaleY);sx=qBound(0.01,m_imageTransformStartScaleX*ratio,100.0);sy=qBound(0.01,m_imageTransformStartScaleY*ratio,100.0);}
            imageLayer->imageScaleX=sx;imageLayer->imageScaleY=sy;
            imageLayer->offsetx=qRound(m_imageTransformCenter.x()-frameSize.width()*sx/2.0);
            imageLayer->offsety=qRound(m_imageTransformCenter.y()-frameSize.height()*sy/2.0);
        }
        update();return;
    }

    if (m_rasterPainting && (e->buttons() & Qt::LeftButton)) {
        LayerPtr paintLayer = ed.activeLayer();
        const bool editingMaskNow = isEditingRasterMask(ed, paintLayer);
        const bool imagePaintNow = paintLayer && paintLayer->type == LayerType::Image &&
            (paintLayer->imagePaintLayer || paintLayer->alphaLock);
        if (!paintLayer || !(editingMaskNow || imagePaintNow)) {
            m_rasterPainting = false;
            m_rasterPixelPerfectPendingValid = false;
            return;
        }
        const QPointF rawCurrent = paintLayer->type == LayerType::Image
            ? mapToImageLocalPoint(paintLayer, mp) : mapToLayerPoint(paintLayer, mp);
        const QPointF current = paint::rasterBrushSnapPoint(ed.session.rasterBrush, rawCurrent);
        QRectF dirty;

        auto applyDab = [&](const QPointF& point, double angle) {
            QRectF dab;
            if (editingMaskNow) {
                RasterBrushSettings maskBrush = ed.session.rasterBrush;
                maskBrush.blendMode = QStringLiteral("source-over");
                dab = paint::rasterBrushDab(&paintLayer->imageMask, maskBrush, point,
                                            m_rasterErasing, angle, true, false,
                                            m_rasterUseClipRegion ? &m_rasterClipRegion : nullptr);
            } else {
                dab = paint::rasterBrushDab(paintLayer, ed.session.rasterBrush, point, m_rasterErasing, angle);
            }
            if (!dab.isEmpty()) dirty = dirty.isEmpty() ? dab : dirty.united(dab);
        };

        if (ed.session.rasterBrush.pixelArt()) {
            // Bresenham na grade artística: cada célula do caminho recebe um
            // dab inteiro, sem subpixel, interpolação ou buracos diagonais.
            const QVector<QPointF> points = paint::rasterBrushPixelLine(ed.session.rasterBrush,
                                                                         m_rasterLastLocal, current);
            if (ed.session.rasterBrush.pixelPerfectActive()) {
                // Um ponto de atraso permite enxergar A-B-C antes de carimbar B.
                // Quando A e C já são vizinhos diagonais e B só forma o canto
                // ortogonal redundante, B é descartado ("double pixel").
                for (const QPointF& point : points) {
                    if (!m_rasterPixelPerfectPendingValid) {
                        m_rasterPixelPerfectPending = point;
                        m_rasterPixelPerfectPendingValid = true;
                        continue;
                    }
                    if (!paint::rasterBrushPixelPerfectSkipMiddle(ed.session.rasterBrush,
                                                                  m_rasterPixelPerfectAnchor,
                                                                  m_rasterPixelPerfectPending,
                                                                  point)) {
                        applyDab(m_rasterPixelPerfectPending, 0.0);
                        m_rasterPixelPerfectAnchor = m_rasterPixelPerfectPending;
                    }
                    m_rasterPixelPerfectPending = point;
                }
            } else {
                for (const QPointF& point : points) applyDab(point, 0.0);
            }
            m_rasterSpacingCarry = 0.0;
        } else {
            QLineF path(m_rasterLastLocal, current);
            const double length = path.length();
            const int spacingPx = paint::rasterBrushSpacingPx(ed.session.rasterBrush);
            if (length > 0.001) {
                constexpr double kPi = 3.14159265358979323846;
                const double angle = std::atan2(current.y() - m_rasterLastLocal.y(),
                                                current.x() - m_rasterLastLocal.x()) * 180.0 / kPi;
                double distance = spacingPx - m_rasterSpacingCarry;
                while (distance <= length + 0.0001) {
                    applyDab(path.pointAt(qBound(0.0, distance / length, 1.0)), angle);
                    distance += spacingPx;
                }
                m_rasterSpacingCarry = std::fmod(m_rasterSpacingCarry + length, double(spacingPx));
            }
        }
        m_rasterLastLocal = current;
        if (!dirty.isEmpty()) {
            ed.markLayerEditRasterDirty(m_rasterSession, dirty.toAlignedRect(), editingMaskNow);
            updateMapRect(rasterLocalRectToMapRect(paintLayer, dirty), 5);
        }
        return;
    }

    if (!m_dragObjectId.isEmpty() && l && l->type == LayerType::Object) {
        const QRectF before = selectedObjectBounds(l);
        const QPointF localMp = mapToLayerPoint(l, mp);
        for (MapObject& o : l->objects) {
            if (o.id != m_dragObjectId) continue;
            if (m_rotatingObject) {
                constexpr double kPi = 3.14159265358979323846;
                const QPointF center = m_dragStartRect.center();
                double pointerAngle = std::atan2(localMp.y() - center.y(),
                                                 localMp.x() - center.x()) * 180.0 / kPi;
                double angle = pointerAngle + m_rotationGrabOffset;
                if (e->modifiers() & Qt::ShiftModifier) angle = std::round(angle / 15.0) * 15.0;
                angle = std::fmod(angle, 360.0);
                if (angle > 180.0) angle -= 360.0;
                if (angle <= -180.0) angle += 360.0;
                o.rotation = angle;
            } else if (m_dragHandle >= 0) {
                QRectF r = m_dragStartRect;
                MapObject startObject = o;
                startObject.x = m_dragStartRect.x();
                startObject.y = m_dragStartRect.y();
                startObject.w = m_dragStartRect.width();
                startObject.h = m_dragStartRect.height();
                startObject.rotation = m_dragStartRotation;
                const QPointF resizePoint = paint::unrotateObjectPoint(startObject, localMp);
                switch (m_dragHandle) {
                case 0: r.setTopLeft(resizePoint);     break;
                case 1: r.setTop(resizePoint.y());     break;
                case 2: r.setTopRight(resizePoint);    break;
                case 3: r.setLeft(resizePoint.x());    break;
                case 4: r.setRight(resizePoint.x());   break;
                case 5: r.setBottomLeft(resizePoint);  break;
                case 6: r.setBottom(resizePoint.y());  break;
                case 7: r.setBottomRight(resizePoint); break;
                default: break;
                }
                r = r.normalized();
                if (ed.session.snapObjects || ed.session.heldSnap) {
                    const int g = qMax(1, ed.session.snapGridSize);
                    r = QRectF(std::round(r.x() / g) * g, std::round(r.y() / g) * g,
                               qMax(double(g), std::round(r.width() / g) * g),
                               qMax(double(g), std::round(r.height() / g) * g));
                }
                // Mantem a alca oposta visualmente fixa mesmo quando o objeto
                // esta rotacionado. Sem esta compensacao, redimensionar faria
                // o objeto "andar" porque o centro de rotacao muda.
                static const int opposite[8] = {7, 6, 5, 4, 3, 2, 1, 0};
                const QVector<QRectF> beforeHandles = paint::objectHandles(startObject, m_zoom);
                const QPointF fixedBefore = beforeHandles.value(opposite[m_dragHandle]).center();
                MapObject candidate = o;
                candidate.x = r.x(); candidate.y = r.y();
                candidate.w = qMax(1.0, r.width()); candidate.h = qMax(1.0, r.height());
                candidate.rotation = m_dragStartRotation;
                const QVector<QRectF> afterHandles = paint::objectHandles(candidate, m_zoom);
                const QPointF fixedAfter = afterHandles.value(opposite[m_dragHandle]).center();
                const QPointF correction = fixedBefore - fixedAfter;
                candidate.x += correction.x();
                candidate.y += correction.y();
                o = candidate;
            } else {
                double nx = localMp.x() - m_dragOffset.x(), ny = localMp.y() - m_dragOffset.y();
                if (ed.session.snapObjects || ed.session.heldSnap) {
                    const int g = qMax(1, ed.session.snapGridSize);
                    nx = std::round(nx / g) * g;
                    ny = std::round(ny / g) * g;
                }
                const double dx = nx - o.x, dy = ny - o.y;
                o.x = nx; o.y = ny;
                for (MapObject& other : l->objects)
                    if (other.id != o.id && ed.session.selectedObjectIds.contains(other.id)) {
                        other.x += dx; other.y += dy;
                    }
            }
            break;
        }
        const QRectF after = selectedObjectBounds(l);
        updateMapRect(before.united(after), 6);
        return;
    }

    if (m_painting && l && l->type == LayerType::Tile) {
        if (m_shapeActive) {
            const QRectF before = shapePreviewMapRect();
            m_shapeEnd = m_hoverCell;
            const QRectF after = shapePreviewMapRect();
            updateMapRect(before.united(after), 5);
            return;
        }
        if (m_hoverCell != m_lastCell) {
            const QPoint from = m_lastCell;
            QSet<quint64>* scatterVisited =
                (ed.session.randomMode && ed.session.randomScattering && !ed.session.randomGridFree)
                    ? &m_randomScatterVisited : nullptr;
            const QVector<QPoint> strokePoints = paint::linePoints(from.x(), from.y(),
                                                                   m_hoverCell.x(), m_hoverCell.y());
            for (int strokeIndex = 0; strokeIndex < strokePoints.size(); ++strokeIndex) {
                // O primeiro ponto é a célula já pintada no evento anterior.
                // Ignorá-lo evita duplicação e mantém o espaçamento contínuo
                // mesmo quando o mouse entrega vários segmentos pequenos.
                if (strokeIndex == 0) continue;
                ++m_brushSpacingCounter;
                if ((m_brushSpacingCounter % qMax(1, ed.session.brush.spacing)) != 0) continue;
                const QPoint& pt = strokePoints.at(strokeIndex);
                const bool semantic = semanticAutotileActive();
                switch (ed.session.tool) {
                case Tool::Terrain:
                case Tool::Stamp:
                case Tool::Eraser:
                    if (semantic) {
                        QVector<QPoint> cells;
                        for (const QPoint& o : paint::brushOffsets(ed.session.brush)) cells.push_back(pt + o);
                        applySemanticAutotileCells(l, cells, m_erasing || ed.session.wangEraseMode || ed.session.tool == Tool::Eraser);
                    } else if (ed.session.tool == Tool::Terrain) {
                        applyTerrainAt(l, pt.x(), pt.y(), m_erasing || ed.session.wangEraseMode);
                    } else if (ed.session.tool == Tool::Eraser) {
                        paint::brushStroke(ed, l, pt.x(), pt.y(), true);
                        if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                            wang::retileTerrainNeighborhood(ed, l, regularPaintFootprint(ed, pt.x(), pt.y()));
                    } else {
                        paint::brushStroke(ed, l, pt.x(), pt.y(), m_erasing, scatterVisited);
                        if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                            wang::retileTerrainNeighborhood(ed, l, regularPaintFootprint(ed, pt.x(), pt.y()));
                    }
                    break;
                case Tool::Fill: break;
                default:
                    paint::brushStroke(ed, l, pt.x(), pt.y(), m_erasing, scatterVisited);
                    if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                        wang::retileTerrainNeighborhood(ed, l, regularPaintFootprint(ed, pt.x(), pt.y()));
                    break;
                }
            }
            invalidateMaskPathCache(l->id);
            const QRectF changed = hoverVisualMapRect(from, oldHoverMap).united(newHoverDirty);
            m_lastCell = m_hoverCell;
            updateMapRect(changed, 4);
            return;
        }
    }

    if (!oldHoverDirty.isEmpty() || !newHoverDirty.isEmpty())
        updateMapRect(oldHoverDirty.united(newHoverDirty), 4);
}

void MapView::mouseReleaseEvent(QMouseEvent* e)
{
    updateHeldModifiers(e->modifiers());
    const QPointF mp = screenToMap(e->position());

    if (e->button() == Qt::RightButton && m_rightEraseCandidate && m_rightEraseActive) {
        const LayerPtr layer = ed.activeLayer();
        if (layer && layer->type == LayerType::Tile) {
            const LayerEffectiveState st = layerEffectiveState(ed.layers(), layer->id);
            if (st.visible && !st.locked) {
                Editor::EditSession eraseSession = ed.beginLayerEdit(layer);
                paint::rectFill(ed, layer,
                                m_rightEraseStart.x(), m_rightEraseStart.y(),
                                m_rightEraseLast.x(), m_rightEraseLast.y(),
                                false, true);
                // Uma área retangular pode abrir bordas em qualquer Terrain da
                // região; normalizar a camada evita variantes Wang quebradas.
                if (!(e->modifiers() & Qt::ShiftModifier))
                    if (!(ed.session.placeOnTop || ed.session.heldStack))
                        wang::retileTerrainLayer(ed, layer);
                invalidateMaskPathCache(layer->id);
                ed.commitLayerEdit(eraseSession, tr("Apagar área com borracha"));

                const QRect cells = normalizedCellRect(m_rightEraseStart, m_rightEraseLast);
                emit statusMessage(tr("Área apagada: %1×%2 tiles.").arg(cells.width()).arg(cells.height()));
                updateMapRect(QRectF(cells.x() * layer->tileWidth + layer->offsetx,
                                     cells.y() * layer->tileHeight + layer->offsety,
                                     cells.width() * layer->tileWidth,
                                     cells.height() * layer->tileHeight), 6);
            }
        }
        m_rightEraseCandidate = false;
        m_rightEraseActive = false;
        m_rightEraseSession.valid = false;
        return;
    }
    if (e->button() == Qt::RightButton && m_tilePickDrag) {
        const LayerPtr layer = ed.activeLayer();
        const QPoint start = m_tilePickStart, end = m_tilePickEnd;
        m_tilePickDrag = false;
        if (layer && layer->type == LayerType::Tile) {
            const QRect cells = normalizedCellRect(start, end);
            const QRectF dirty(cells.x() * layer->tileWidth + layer->offsetx,
                               cells.y() * layer->tileHeight + layer->offsety,
                               cells.width() * layer->tileWidth, cells.height() * layer->tileHeight);
            if (start == end) {
                pickPlacedTileAt(mp, e->modifiers() & Qt::ShiftModifier);
            } else {
                CustomStamp stamp = paint::stampFromMap(layer, start.x(), start.y(), end.x(), end.y());
                if (stamp.valid()) {
                    ed.session.customStamp = stamp;
                    ed.session.activeAutotileId.clear();
                    ed.session.wangBrushActive = false;
                    // Mantém a ferramenta atual. Assim a região capturada pode ser
                    // usada imediatamente com Pincel, Retângulo, Círculo, Linha
                    // ou Balde sem dessintonizar o QAction marcado na Toolbar.
                    emit ed.selectionChanged();
                    emit statusMessage(tr("✅ Seleção %1×%2 capturada — tiles normais e Autotiles preservados. Ferramenta atual mantida.")
                                           .arg(stamp.w).arg(stamp.h));
                } else {
                    emit statusMessage(tr("⚠ A seleção não contém tiles para capturar."));
                }
            }
            updateMapRect(dirty, 5);
        }
        return;
    }
    if (e->button() == Qt::LeftButton && m_editSelecting) {
        finishEditSelection(mp);
        return;
    }
    if (e->button() == Qt::LeftButton && m_gridFreePainting) {
        m_gridFreePainting = false;
        if (m_gridFreeSession.valid) ed.commitLayerEdit(m_gridFreeSession, tr("Aleatório Sem grade / Dispersão"));
        m_gridFreeSession.valid = false;
        m_gridFreeLayerId.clear();
        update();
        return;
    }
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        return;
    }
    endStroke(mp, e->modifiers());
}

void MapView::endStroke(const QPointF& mapPos, Qt::KeyboardModifiers)
{
    if (m_regionPainting) {
        m_regionPainting = false;
        MapDoc* d = ed.doc();
        if (d && m_regionShapeActive) {
            const QVector<QPoint> cells = semanticShapeCells(ed.session.tool, m_regionShapeStart,
                                                              m_regionShapeEnd, !ed.session.shapeFilled);
            for (const QPoint& cell : cells) paintRegionCell(cell, m_regionErase);
        }
        updateRegionAreaGradient();
        m_regionShapeActive = false;
        QVector<RegionHistoryChange> changes;
        changes.reserve(m_regionBeforeValues.size());
        if (d) {
            for (auto it = m_regionBeforeValues.cbegin(); it != m_regionBeforeValues.cend(); ++it) {
                const int x = MapDoc::regionX(it.key()), y = MapDoc::regionY(it.key());
                const int after = d->regionIdAt(x, y);
                if (it.value() != after)
                    changes.push_back(RegionHistoryChange{x, y, it.value(), after});
            }
            QString label;
            if (m_regionErase) label = tr("Apagar regiões");
            else if (ed.session.tool == Tool::Rect) label = tr("Retângulo de região %1").arg(ed.session.activeRegionId);
            else if (ed.session.tool == Tool::Circle) label = tr("Círculo de região %1").arg(ed.session.activeRegionId);
            else if (ed.session.tool == Tool::Line) label = tr("Linha de região %1").arg(ed.session.activeRegionId);
            else if (ed.session.tool == Tool::Fill) label = tr("Preencher região %1").arg(ed.session.activeRegionId);
            else label = tr("Pintar região %1").arg(ed.session.activeRegionId);
            ed.pushRegionHistory(changes, m_regionBeforeAuthored, label);
        }
        m_regionBeforeValues.clear();
        const QPoint c = regionCellAt(mapPos);
        if (d) updateMapRect(QRectF(c.x() * d->map.tileWidth, c.y() * d->map.tileHeight,
                                    d->map.tileWidth, d->map.tileHeight), 4);
        return;
    }

    const LayerPtr l = ed.activeLayer();

    if (m_slopeSelecting) {
        m_slopeSelecting = false;
        if (!l || (l->type != LayerType::Tile && l->type != LayerType::Image)) { update(); return; }
        m_slopeEndMap = mapPos;
        QRect localRect;
        if (l->type == LayerType::Tile) {
            const QPoint a = cellAt(m_slopeStartMap, l);
            const QPoint b = cellAt(m_slopeEndMap, l);
            QRect cells = normalizedCellRect(a, b).intersected(QRect(0, 0, l->cols, l->rows));
            if (!cells.isEmpty())
                localRect = QRect(cells.x() * l->tileWidth, cells.y() * l->tileHeight,
                                  cells.width() * l->tileWidth, cells.height() * l->tileHeight);
        } else {
            // Para Image Layers transformadas, converta os quatro cantos do
            // retangulo visual para o espaco-fonte e use o bounding rect local.
            QRectF mapRect(m_slopeStartMap, m_slopeEndMap);
            mapRect = mapRect.normalized();
            if (mapRect.width() < 1.0 && mapRect.height() < 1.0) {
                localRect = l->image.rect(); // clique simples = imagem inteira
            } else {
                QPolygonF localPoly;
                localPoly << mapToImageLocalPoint(l, mapRect.topLeft())
                          << mapToImageLocalPoint(l, mapRect.topRight())
                          << mapToImageLocalPoint(l, mapRect.bottomRight())
                          << mapToImageLocalPoint(l, mapRect.bottomLeft());
                localRect = localPoly.boundingRect().toAlignedRect().intersected(l->image.rect());
            }
        }
        update();
        if (!localRect.isEmpty()) emit slopeSelectionRequested(l->id, localRect);
        else emit statusMessage(tr("A seleção da Inclinação não encontrou conteúdo nesta camada."));
        return;
    }

    if (m_imageDragging) {
        m_imageDragging = false;
        LayerPtr imageLayer = ed.findNode(m_imageDragLayerId);
        m_imageDragLayerId.clear();
        if (!m_imageDragStartOffsets.isEmpty()) {
            const int movedCount = m_imageDragStartOffsets.size();
            m_imageDragStartOffsets.clear();
            ed.pushDocHistory(m_imageGroupDragBefore,
                              movedCount == 1
                                  ? tr("Mover camada de imagem") : tr("Mover camadas"));
            emit ed.mapChanged();
            emit ed.layersChanged();
        } else if (m_imageDragSession.valid) {
            ed.commitLayerEdit(m_imageDragSession, tr("Mover camada de imagem"));
        }
        m_imageDragSession.valid = false;
        if (imageLayer) updateMapRect(imageLocalRectToMapRect(imageLayer, imageVisualLocalRect(imageLayer)), 6);
        return;
    }
    if(m_imageTransformHandle){
        const bool rotating=m_imageTransformHandle==2;m_imageTransformHandle=0;m_imageDragLayerId.clear();
        if(m_imageDragSession.valid)ed.commitLayerEdit(m_imageDragSession,rotating?tr("Girar camada de imagem"):tr("Redimensionar camada de imagem"));
        m_imageDragSession.valid=false;update();return;
    }

    if (m_rasterPainting) {
        // O Pixel-Perfect mantém o último ponto pendente até existir contexto
        // suficiente para decidir se o ponto anterior era redundante. No fim
        // do gesto, o último ponto sempre pertence ao traço e precisa ser salvo.
        if (m_rasterPixelPerfectPendingValid && l) {
            const bool editingMaskNow = isEditingRasterMask(ed, l);
            QRectF finalDab;
            if (editingMaskNow) {
                RasterBrushSettings maskBrush = ed.session.rasterBrush;
                maskBrush.blendMode = QStringLiteral("source-over");
                finalDab = paint::rasterBrushDab(&l->imageMask, maskBrush,
                                                 m_rasterPixelPerfectPending,
                                                 m_rasterErasing, 0.0, true, false,
                                                 m_rasterUseClipRegion ? &m_rasterClipRegion : nullptr);
            } else {
                finalDab = paint::rasterBrushDab(l, ed.session.rasterBrush,
                                                 m_rasterPixelPerfectPending,
                                                 m_rasterErasing, 0.0);
            }
            if (!finalDab.isEmpty())
                ed.markLayerEditRasterDirty(m_rasterSession, finalDab.toAlignedRect(), editingMaskNow);
        }
        m_rasterPixelPerfectPendingValid = false;
        m_rasterPainting = false;
        m_rasterErasing = false;
        m_rasterSpacingCarry = 0.0;
        ed.commitLayerEdit(m_rasterSession,
                           isEditingRasterMask(ed, l)
                               ? tr("Pintar máscara de “%1”").arg(l ? l->name : tr("camada"))
                               : tr("Pintura em “%1”").arg(l ? l->name : tr("camada")));
        m_rasterSession.valid = false;
        m_rasterUseClipRegion = false;
        m_rasterClipRegion = QRegion();
        update();
        return;
    }

    if (m_marquee && l && l->type == LayerType::Object) {
        const QRectF r = QRectF(m_marqueeStart, m_marqueeEnd).normalized();
        ed.session.selectedObjectIds.clear();
        for (const MapObject& o : l->objects)
            if (r.intersects(objectMapRect(l, o))) ed.session.selectedObjectIds.insert(o.id);
        ed.session.selectedObjectId = ed.session.selectedObjectIds.isEmpty() ? QString() : *ed.session.selectedObjectIds.begin();
        m_marquee = false;
        emit ed.selectionChanged();
        update();
        return;
    }
    if (!m_dragObjectId.isEmpty()) {
        const QRectF dirty = selectedObjectBounds(l);
        const bool wasRotating = m_rotatingObject;
        m_dragObjectId.clear();
        m_dragHandle = -1;
        m_rotatingObject = false;
        m_suppressFullMapUpdate = true;
        ed.commitLayerEdit(m_session, wasRotating ? tr("Girar objeto")
                                                  : tr("Mover/redimensionar objeto"));
        m_suppressFullMapUpdate = false;
        updateMapRect(dirty, 6);
        return;
    }
    if (!m_painting) return;
    m_painting = false;

    QRectF finalDirty = hoverVisualMapRect(m_hoverCell, m_hoverMap);
    if (m_shapeActive && l && l->type == LayerType::Tile) {
        finalDirty = shapePreviewMapRect();
        const bool hollow = !ed.session.shapeFilled;
        if (semanticAutotileActive()) {
            const QVector<QPoint> cells = semanticShapeCells(ed.session.tool, m_shapeStart, m_shapeEnd, hollow);
            applySemanticAutotileCells(l, cells, m_erasing || ed.session.wangEraseMode);
        } else {
            switch (ed.session.tool) {
            case Tool::Rect:
                paint::rectFill(ed, l, m_shapeStart.x(), m_shapeStart.y(),
                                m_shapeEnd.x(), m_shapeEnd.y(), hollow, m_erasing);
                break;
            case Tool::Circle:
                paint::circleFill(ed, l, m_shapeStart.x(), m_shapeStart.y(),
                                  m_shapeEnd.x(), m_shapeEnd.y(), hollow, m_erasing);
                break;
            case Tool::Line:
                paint::lineStroke(ed, l, m_shapeStart.x(), m_shapeStart.y(),
                                  m_shapeEnd.x(), m_shapeEnd.y(), m_erasing);
                break;
            default: break;
            }
            if (!m_preserveAutotileShape && !(ed.session.placeOnTop || ed.session.heldStack))
                wang::retileTerrainNeighborhood(ed, l, normalizedCellRect(m_shapeStart, m_shapeEnd));
        }
        invalidateMaskPathCache(l->id);
        m_shapeActive = false;
    }
    m_suppressFullMapUpdate = true;
    ed.commitLayerEdit(m_session, tr("%1 em “%2”").arg(toolLabel(ed.session.tool), l ? l->name : QString()));
    m_suppressFullMapUpdate = false;
    m_randomScatterVisited.clear();
    m_preserveAutotileShape = false;
    updateMapRect(finalDirty, 5);
}

void MapView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        setZoom(m_zoom * factor, e->position());
        e->accept();
        return;
    }
    const double dx = e->angleDelta().x() / (2.0 * m_zoom);
    const double dy = e->angleDelta().y() / (2.0 * m_zoom);
    m_pan += QPointF(-dx, -dy);
    clampPan();
    emit viewportChanged();
    update();
    e->accept();
}

void MapView::leaveEvent(QEvent*)
{
    const QRectF dirty = m_hasHover ? hoverVisualMapRect(m_hoverCell, m_hoverMap) : QRectF();
    m_hasHover = false;
    updateMapRect(dirty, 4);
}

void MapView::updateHeldModifiers(Qt::KeyboardModifiers mods)
{
    const LayerPtr layer = ed.activeLayer();
    const bool objects = !ed.session.regionMarkMode && layer && layer->type == LayerType::Object;
    const auto held = core::heldEditorModes(objects, mods & Qt::ControlModifier, mods & Qt::ShiftModifier);
    ed.session.heldSnap = held.snap; ed.session.heldStack = held.stack; ed.session.heldErase = held.erase;
    if (m_painting) m_erasing = ed.session.heldErase || ed.session.tool == Tool::Eraser;
    if (m_rasterPainting) m_rasterErasing = ed.session.heldErase || ed.session.tool == Tool::Eraser;
    if (m_regionPainting) m_regionErase = ed.session.heldErase || ed.session.activeRegionId == 0 || ed.session.tool == Tool::Eraser;
}

void MapView::keyReleaseEvent(QKeyEvent* e)
{
    updateHeldModifiers(e->modifiers());
    update();
    QWidget::keyReleaseEvent(e);
}

void MapView::focusOutEvent(QFocusEvent* e)
{
    updateHeldModifiers(Qt::NoModifier);
    update();
    QWidget::focusOutEvent(e);
}

void MapView::keyPressEvent(QKeyEvent* e)
{
    updateHeldModifiers(e->modifiers());
    update();
    const LayerPtr active = ed.activeLayer();
    if (active && active->type == LayerType::Object && ed.session.tool == Tool::Object &&
        (e->key()==Qt::Key_Z || e->key()==Qt::Key_X || e->key()==Qt::Key_C) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        int& index = ed.session.objectInsertionIndex;
        const int count = active->objects.size();
        if (e->key() == Qt::Key_Z) index = core::objectDepthBack(index,count);
        else if (e->key() == Qt::Key_X) { index = core::objectDepthForward(index,count); }
        else if (e->key() == Qt::Key_C) index = -1;
        else { QWidget::keyPressEvent(e); return; }
        emit statusMessage(index < 0 ? tr("Profundidade: à frente de todos (Z: recuar, X: avançar, C: frente)")
                                     : tr("Profundidade dos próximos objetos: %1 (Z / X; C restaura frente)").arg(index));
        e->accept(); update(); return;
    }
    const LayerPtr l = ed.activeLayer();

    // Atalhos básicos usam o mesmo roteador de contexto do menu Editar.
    // Isto cobre Tile, Regiões, Paint/Mask, Object e Image sem cada ferramenta
    // inventar uma semântica diferente para Ctrl+C/Delete.
    const bool ctrlEdit = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    if (ctrlEdit && e->key() == Qt::Key_A && editSelectAll()) { e->accept(); return; }
    if (ctrlEdit && e->key() == Qt::Key_C && editCopy()) { e->accept(); return; }
    if (ctrlEdit && e->key() == Qt::Key_X && editCut()) { e->accept(); return; }
    if (ctrlEdit && e->key() == Qt::Key_V && editPaste()) { e->accept(); return; }
    if (e->key() == Qt::Key_Delete && editDelete()) { e->accept(); return; }
    if (e->key() == Qt::Key_Escape && editClearSelection()) { e->accept(); return; }

    // Image Layer / Slope: seleção direta, teclado e clipboard interno.
    if (l && l->type == LayerType::Image && ed.session.tool == Tool::Select) {
        const bool ctrl = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
        if (ctrl && e->key() == Qt::Key_C) {
            ed.session.clipboardLayer = cloneLayer(l, false);
            emit statusMessage(tr("Camada “%1” copiada. Ctrl+V cola uma nova cópia.").arg(l->name));
            e->accept(); return;
        }
        if (ctrl && e->key() == Qt::Key_V && ed.session.clipboardLayer &&
            ed.session.clipboardLayer->type == LayerType::Image) {
            const DocSnapshot before = ed.snapshotDoc();
            LayerPtr copy = cloneLayer(ed.session.clipboardLayer, true);
            copy->name += tr(" (cópia)");
            const int bump = ed.session.snapObjects ? qMax(1, ed.session.snapGridSize) : 16;
            copy->offsetx += bump; copy->offsety += bump;
            if (ed.session.snapObjects) {
                const int grid = qMax(1, ed.session.snapGridSize);
                copy->offsetx = qRound(double(copy->offsetx) / grid) * grid;
                copy->offsety = qRound(double(copy->offsety) / grid) * grid;
            }
            ed.addLayer(copy);
            ed.pushDocHistory(before, tr("Colar camada de imagem"));
            emit statusMessage(tr("Camada colada — arraste com Seleção para reposicionar."));
            update(); e->accept(); return;
        }
        if (ctrl && e->key() == Qt::Key_D) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.duplicateLayer(l->id);
            if (LayerPtr copy = ed.selectedLayer()) {
                if (copy->type == LayerType::Image) {
                    const int bump = ed.session.snapObjects ? qMax(1, ed.session.snapGridSize) : 16;
                    copy->offsetx += bump; copy->offsety += bump;
                    if (ed.session.snapObjects) {
                        const int grid = qMax(1, ed.session.snapGridSize);
                        copy->offsetx = qRound(double(copy->offsetx) / grid) * grid;
                        copy->offsety = qRound(double(copy->offsety) / grid) * grid;
                    }
                }
            }
            ed.pushDocHistory(before, tr("Duplicar camada de imagem"));
            emit statusMessage(ed.session.snapObjects
                ? tr("Camada duplicada e deslocada 1 passo da grade (%1 px).").arg(ed.session.snapGridSize)
                : tr("Camada duplicada e deslocada 16 px."));
            update(); e->accept(); return;
        }
        int dx = 0, dy = 0;
        switch (e->key()) {
        case Qt::Key_Left:  dx = -1; break;
        case Qt::Key_Right: dx =  1; break;
        case Qt::Key_Up:    dy = -1; break;
        case Qt::Key_Down:  dy =  1; break;
        default: break;
        }
        if (dx || dy) {
            const DocSnapshot before = ed.snapshotDoc();
            int moveX = 0, moveY = 0;
            if (ed.session.snapObjects || ed.session.heldSnap) {
                const int grid = qMax(1, ed.session.snapGridSize);
                const bool large = e->modifiers() & Qt::ShiftModifier;
                auto nextGrid = [grid, large](int value, int direction) {
                    if (!direction) return value;
                    if (large) return qRound(double(value) / grid) * grid + direction * grid * 10;
                    if (direction > 0) return int(std::floor(double(value) / grid) + 1.0) * grid;
                    return int(std::ceil(double(value) / grid) - 1.0) * grid;
                };
                const int nextX = nextGrid(l->offsetx, dx);
                const int nextY = nextGrid(l->offsety, dy);
                moveX = nextX - l->offsetx;
                moveY = nextY - l->offsety;
            } else {
                const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
                moveX = dx * step; moveY = dy * step;
            }
            int moved = 0;
            for (const LayerPtr& selected : ed.selectedLayers()) {
                if (!selected || selected->type == LayerType::Group) continue;
                const LayerEffectiveState selectedState = layerEffectiveState(ed.layers(), selected->id);
                if (selectedState.locked || !selectedState.visible) continue;
                selected->offsetx += moveX;
                selected->offsety += moveY;
                ++moved;
            }
            ed.pushDocHistory(before, moved > 1 ? tr("Mover camadas") : tr("Mover camada de imagem"));
            emit ed.mapChanged();
            emit ed.layersChanged();
            update(); e->accept(); return;
        }
    }

    // Setas movem objetos selecionados (1px, Ctrl = 10px), como no original.
    if (l && l->type == LayerType::Object && !ed.session.selectedObjectIds.isEmpty()) {
        int dx = 0, dy = 0;
        switch (e->key()) {
        case Qt::Key_Left:  dx = -1; break;
        case Qt::Key_Right: dx =  1; break;
        case Qt::Key_Up:    dy = -1; break;
        case Qt::Key_Down:  dy =  1; break;
        default: break;
        }
        if (dx || dy) {
            const int step = (e->modifiers() & Qt::ControlModifier) ? 10 : 1;
            Editor::EditSession s = ed.beginLayerEdit(l);
            for (MapObject& o : l->objects)
                if (ed.session.selectedObjectIds.contains(o.id)) { o.x += dx * step; o.y += dy * step; }
            ed.commitLayerEdit(s, tr("Mover objeto"));
            update();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Delete) {
            Editor::EditSession s = ed.beginLayerEdit(l);
            for (int i = l->objects.size() - 1; i >= 0; --i)
                if (ed.session.selectedObjectIds.contains(l->objects[i].id)) l->objects.remove(i);
            ed.session.selectedObjectIds.clear();
            ed.session.selectedObjectId.clear();
            ed.commitLayerEdit(s, tr("Excluir objeto"));
            emit ed.selectionChanged();
            update();
            e->accept();
            return;
        }
        if ((e->key() == Qt::Key_BracketLeft || e->key() == Qt::Key_BracketRight) &&
            !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            const double delta = e->key() == Qt::Key_BracketLeft ? -15.0 : 15.0;
            Editor::EditSession s = ed.beginLayerEdit(l);
            for (MapObject& o : l->objects) {
                if (!ed.session.selectedObjectIds.contains(o.id) && o.id != ed.session.selectedObjectId) continue;
                o.rotation = std::fmod(o.rotation + delta, 360.0);
                if (o.rotation > 180.0) o.rotation -= 360.0;
                if (o.rotation <= -180.0) o.rotation += 360.0;
            }
            ed.commitLayerEdit(s, tr("Girar objeto"));
            emit statusMessage(tr("Rotação: [ = -15° · ] = +15° · Shift ao arrastar = encaixe de 15°"));
            update();
            e->accept();
            return;
        }
    }
    QWidget::keyPressEvent(e);
}

} // namespace ui
