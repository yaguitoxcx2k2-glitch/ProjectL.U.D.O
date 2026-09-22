#include "Minimap.h"

#include "core/Renderer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

using namespace core;

namespace ui {

Minimap::Minimap(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    setMinimumHeight(120);
    setToolTip(tr("Minimapa — clique ou arraste para navegar."));
    // Redesenhar a miniatura e caro; agendamos com um timer de coalescencia.
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(220);
    connect(timer, &QTimer::timeout, this, [this] { m_dirty = true; update(); });
    connect(&ed, &Editor::mapChanged, this, [timer] { timer->start(); });
    connect(&ed, &Editor::layersChanged, this, [timer] { timer->start(); });
    connect(&ed, &Editor::docsChanged, this, [timer] { timer->start(); });
}

void Minimap::setViewport(const QRectF& r)
{
    m_viewport = r;
    update();
}

void Minimap::resizeEvent(QResizeEvent*)
{
    m_dirty = true;
}

QRectF Minimap::mapArea() const
{
    const MapInfo& info = ed.mapInfo();
    if (info.pixelWidth() <= 0 || info.pixelHeight() <= 0) return QRectF();
    const double s = qMin(double(width() - 8) / info.pixelWidth(),
                          double(height() - 8) / info.pixelHeight());
    const double w = info.pixelWidth() * s, h = info.pixelHeight() * s;
    return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
}

void Minimap::rebuildCache()
{
    const QRectF area = mapArea();
    if (area.isEmpty()) { m_cache = QImage(); return; }
    RenderOptions opt;
    opt.drawObjectFrames = false;
    m_cache = renderMapToImage(ed, opt, area.size().toSize());
    m_dirty = false;
}

void Minimap::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#151515"));
    if (m_dirty) rebuildCache();
    const QRectF area = mapArea();
    if (m_cache.isNull() || area.isEmpty()) return;

    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(area, m_cache);
    p.setPen(QPen(QColor(0, 0, 0, 180), 1));
    p.drawRect(area);

    if (!m_viewport.isEmpty()) {
        const MapInfo& info = ed.mapInfo();
        const double sx = area.width() / qMax(1, info.pixelWidth());
        const double sy = area.height() / qMax(1, info.pixelHeight());
        const QRectF vp(area.x() + m_viewport.x() * sx, area.y() + m_viewport.y() * sy,
                        m_viewport.width() * sx, m_viewport.height() * sy);
        p.setPen(QPen(QColor("#4a90d7"), 1.5));
        p.setBrush(QColor(74, 144, 215, 30));
        p.drawRect(vp.intersected(area));
    }
}

void Minimap::mousePressEvent(QMouseEvent* e)
{
    mouseMoveEvent(e);
}

void Minimap::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton)) return;
    const QRectF area = mapArea();
    if (area.isEmpty()) return;
    const MapInfo& info = ed.mapInfo();
    const double fx = clampd((e->position().x() - area.x()) / area.width(), 0, 1);
    const double fy = clampd((e->position().y() - area.y()) / area.height(), 0, 1);
    emit navigateRequested(QPointF(fx * info.pixelWidth(), fy * info.pixelHeight()));
}

} // namespace ui
