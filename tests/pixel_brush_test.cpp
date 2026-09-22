#include "core/PaintOps.h"

#include <QColor>
#include <QImage>
#include <QPointF>

#include <cassert>
#include <cmath>

using namespace core;

static bool near(double a, double b)
{
    return std::abs(a - b) < 0.0001;
}

int main()
{
    RasterBrushSettings brush;
    brush.authoringMode = QStringLiteral("pixel-art");
    brush.sizePx = 64; // tamanho do modo normal deve permanecer independente
    brush.pixelSize = 1;
    brush.pixelScale = 4;
    brush.pixelShape = QStringLiteral("square");
    brush.color = QColor(255, 0, 0, 255);
    brush.flow = 100;
    brush.opacity = 100;

    // Cursor e footprint usam somente a grade artística, nunca subpixel.
    const QPointF snapped = paint::rasterBrushSnapPoint(brush, QPointF(5.9, 9.1));
    assert(near(snapped.x(), 6.0));
    assert(near(snapped.y(), 10.0));
    const QRectF footprint = paint::rasterBrushTargetRect(brush, QPointF(5.9, 9.1));
    assert(footprint == QRectF(4, 8, 4, 4));
    assert(brush.sizePx == 64); // alternar para Pixel Art não sobrescreve o tamanho normal

    // A ponta ampliada é nearest-neighbour: um pixel artístico vira bloco 4x4.
    QImage tip = paint::rasterBrushPreviewTip(brush);
    assert(tip.size() == QSize(4, 4));
    for (int y = 0; y < tip.height(); ++y)
        for (int x = 0; x < tip.width(); ++x)
            assert(qAlpha(tip.pixel(x, y)) == 255);

    // Bresenham deve terminar exatamente na célula final e nunca emitir
    // coordenadas fora da grade de pixelScale.
    const QVector<QPointF> line = paint::rasterBrushPixelLine(brush, QPointF(2, 2), QPointF(14, 10));
    assert(!line.isEmpty());
    const QPointF last = line.constLast();
    assert(near(last.x(), 14.0));
    assert(near(last.y(), 10.0));
    for (const QPointF& p : line) {
        assert(near(std::fmod(p.x() - 2.0, 4.0), 0.0));
        assert(near(std::fmod(p.y() - 2.0, 4.0), 0.0));
    }

    // Pixel-Perfect remove somente o canto redundante de uma linha de 1 px.
    // A=(0,0), B=(1,0), C=(1,1): A e C já se tocam diagonalmente, portanto
    // B seria o "double pixel" visual. Linha reta e pincel maior não filtram.
    brush.pixelScale = 1;
    brush.pixelSize = 1;
    brush.pixelPerfect = true;
    assert(paint::rasterBrushPixelPerfectSkipMiddle(
        brush, QPointF(0.5, 0.5), QPointF(1.5, 0.5), QPointF(1.5, 1.5)));
    assert(!paint::rasterBrushPixelPerfectSkipMiddle(
        brush, QPointF(0.5, 0.5), QPointF(1.5, 0.5), QPointF(2.5, 0.5)));
    brush.pixelSize = 2;
    assert(!paint::rasterBrushPixelPerfectSkipMiddle(
        brush, QPointF(0.5, 0.5), QPointF(1.5, 0.5), QPointF(1.5, 1.5)));
    brush.pixelPerfect = false;

    // Dithering 50% fica ancorado na grade global e afeta blocos inteiros.
    brush.pixelScale = 4;
    brush.pixelDither = QStringLiteral("50");
    brush.pixelSize = 2;
    QImage dithered = paint::rasterBrushPreviewTipAt(brush, QPointF(6, 6));
    assert(dithered.size() == QSize(8, 8));
    const bool blockA = qAlpha(dithered.pixel(0, 0)) > 0;
    const bool blockB = qAlpha(dithered.pixel(4, 0)) > 0;
    assert(blockA != blockB);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            assert((qAlpha(dithered.pixel(x, y)) > 0) == blockA);

    // Color Replace é exato: só o pixel com a cor-alvo pode mudar.
    brush.pixelDither = QStringLiteral("none");
    brush.pixelSize = 1;
    brush.pixelScale = 1;
    brush.pixelReplaceEnabled = true;
    brush.pixelReplaceColor = QColor(0, 255, 0, 255);
    brush.color = QColor(255, 0, 0, 255);
    QImage target(3, 1, QImage::Format_ARGB32_Premultiplied);
    target.fill(Qt::transparent);
    target.setPixelColor(0, 0, QColor(0, 255, 0, 255));
    target.setPixelColor(1, 0, QColor(0, 0, 255, 255));
    target.setPixelColor(2, 0, QColor(0, 255, 0, 255));
    paint::rasterBrushDab(&target, brush, QPointF(0.5, 0.5), false);
    assert(target.pixelColor(0, 0) == QColor(255, 0, 0, 255));
    assert(target.pixelColor(1, 0) == QColor(0, 0, 255, 255));

    return 0;
}
