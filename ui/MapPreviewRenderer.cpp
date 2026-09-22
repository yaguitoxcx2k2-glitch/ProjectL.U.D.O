#include "MapPreviewRenderer.h"

#include "core/Renderer.h"

#include <QPainter>
#include <cmath>

namespace ui {
namespace {

QImage transformedPanorama(const core::PanoramaDef& layer)
{
    QImage image = layer.image;
    if (!image.isNull() && (layer.flipH || layer.flipV))
        image = image.mirrored(layer.flipH, layer.flipV);
    return image;
}

void drawPanorama(QPainter& painter, const core::PanoramaDef& layer, const QRect& bounds)
{
    if (!layer.enabled || !layer.showInEditor || layer.opacity <= 0) return;
    const QImage image = transformedPanorama(layer);
    if (image.isNull()) return;
    painter.save();
    painter.setClipRect(bounds);
    painter.setOpacity(qBound(0, layer.opacity, 255) / 255.0);
    switch (layer.blend) {
    case core::PictureBlend::Add:      painter.setCompositionMode(QPainter::CompositionMode_Plus); break;
    case core::PictureBlend::Multiply: painter.setCompositionMode(QPainter::CompositionMode_Multiply); break;
    case core::PictureBlend::Screen:   painter.setCompositionMode(QPainter::CompositionMode_Screen); break;
    default: break;
    }
    const int width = qMax(1, image.width());
    const int height = qMax(1, image.height());
    for (int y = bounds.top(); y <= bounds.bottom(); y += height)
        for (int x = bounds.left(); x <= bounds.right(); x += width)
            painter.drawImage(QPoint(x, y), image);
    painter.restore();
}

} // namespace

QImage renderMapPreview(const core::Editor& editor, const core::MapDoc& doc,
                        const QSize& target)
{
    const QSize native(qMax(1, doc.map.pixelWidth()), qMax(1, doc.map.pixelHeight()));
    const QSize outputSize=target.isValid()&&!target.isEmpty()
        ? native.scaled(target,Qt::KeepAspectRatio) : native;
    QImage output(outputSize, QImage::Format_ARGB32_Premultiplied);
    output.fill(doc.map.background);
    QPainter painter(&output);
    painter.scale(output.width()/double(native.width()),output.height()/double(native.height()));
    const QRect bounds(QPoint(),native);

    if (!doc.environment.panoramas.isEmpty()) {
        for (const core::PanoramaDef& layer : doc.environment.panoramas)
            drawPanorama(painter, layer, bounds);
    } else if (doc.environment.panoramaInEditor && !doc.environment.panorama.isNull()) {
        core::PanoramaDef legacy;
        legacy.image = doc.environment.panorama;
        legacy.showInEditor = true;
        legacy.fixed = doc.environment.panoramaFixed;
        drawPanorama(painter, legacy, bounds);
    }

    core::RenderOptions options;
    options.fillBackground = false;
    options.drawObjectFrames = false;
    const QImage map = core::renderMapToImage(editor, doc, options, outputSize);
    painter.resetTransform();
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setOpacity(1.0);
    painter.drawImage(0, 0, map);
    painter.end();

    return output;
}

} // namespace ui
