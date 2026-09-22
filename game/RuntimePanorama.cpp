#include "RuntimePanorama.h"

#include <QtGlobal>
#include <cmath>

namespace game {
namespace {

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

QVector<double> axisOrigins(double phase, double tileSize,
                            double visibleMin, double visibleMax)
{
    QVector<double> out;
    tileSize = qMax(1.0, finiteOr(tileSize, 1.0));
    phase = finiteOr(phase, 0.0);
    visibleMin = finiteOr(visibleMin, 0.0);
    visibleMax = finiteOr(visibleMax, visibleMin);

    const double first = phase + std::floor((visibleMin - phase) / tileSize) * tileSize;
    // Limite defensivo: uma imagem patológica de 1 px não deve poder criar
    // milhões de quads se a resolução estiver corrompida. O RuntimeRenderState
    // já normaliza a viewport; este teto é a segunda linha de defesa.
    constexpr int maxTilesPerAxis = 8192;
    for (double p = first; p < visibleMax + tileSize && out.size() < maxTilesPerAxis; p += tileSize)
        out.push_back(p);
    return out;
}

} // namespace

RuntimePanoramaLayout buildRuntimePanoramaLayout(const RuntimeRenderState& state,
                                                 const RuntimePanoramaInput& input)
{
    RuntimePanoramaLayout out;
    if (input.imageSize.isEmpty()) return out;

    const double width = qMax(1.0, finiteOr(input.imageSize.width(), 1.0));
    const double height = qMax(1.0, finiteOr(input.imageSize.height(), 1.0));
    const QPointF offset(finiteOr(input.scrollOffset.x(), 0.0),
                         finiteOr(input.scrollOffset.y(), 0.0));

    if (input.fixed) {
        out.space = RuntimeCoordinateSpace::Screen;
        out.visibleRect = state.screenRect();
        out.phase = offset;
    } else {
        out.space = RuntimeCoordinateSpace::World;
        out.visibleRect = state.cameraWorldRect();
        const double px = qBound(0.0, finiteOr(input.parallaxX, 1.0), 4.0);
        const double py = qBound(0.0, finiteOr(input.parallaxY, 1.0), 4.0);
        // Mesma semântica histórica, agora centralizada: 1 acompanha o mapa,
        // 0 cancela apenas o deslocamento da câmera (continua World Space),
        // 0,5 desloca à metade da velocidade.
        out.phase = QPointF(state.camera.x() * (1.0 - px) + offset.x(),
                            state.camera.y() * (1.0 - py) + offset.y());
    }

    // Panorama clássico da LUDO é tiled para cobrir a área. loopX/loopY não
    // entram aqui: eles habilitam o avanço de scroll por velocidade no tick.
    const QVector<double> xs = axisOrigins(out.phase.x(), width,
                                            out.visibleRect.left(), out.visibleRect.right());
    const QVector<double> ys = axisOrigins(out.phase.y(), height,
                                            out.visibleRect.top(), out.visibleRect.bottom());
    if (xs.isEmpty() || ys.isEmpty()) return out;

    // Outro teto defensivo para combinação 8192×8192 em input corrompido.
    constexpr int maxTilesTotal = 65536;
    out.tiles.reserve(qMin(maxTilesTotal, xs.size() * ys.size()));
    for (double y : ys) {
        for (double x : xs) {
            if (out.tiles.size() >= maxTilesTotal) break;
            out.tiles.push_back(QRectF(x, y, width, height));
        }
        if (out.tiles.size() >= maxTilesTotal) break;
    }
    out.valid = !out.tiles.isEmpty();
    return out;
}

} // namespace game
