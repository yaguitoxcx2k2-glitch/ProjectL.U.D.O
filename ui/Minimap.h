// ============================================================================
//  Minimap.h — Minimapa flutuante (equivalente ao canvas #miniCanvas +
//  drawMini/drawMiniViewport). Clique/arraste navega pelo mapa.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include <QWidget>

namespace ui {

class Minimap : public QWidget
{
    Q_OBJECT
public:
    explicit Minimap(core::Editor& ed, QWidget* parent = nullptr);

    /// Retangulo visivel no MapView, em pixels do mapa.
    void setViewport(const QRectF& r);
    QSize sizeHint() const override { return QSize(220, 160); }

signals:
    void navigateRequested(const QPointF& mapPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuildCache();
    QRectF mapArea() const;

    core::Editor& ed;
    QImage  m_cache;
    QRectF  m_viewport;
    bool    m_dirty = true;
};

} // namespace ui
