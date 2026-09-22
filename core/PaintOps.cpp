#include "PaintOps.h"
#include "TilesetOps.h"

#include <QRandomGenerator>
#include <QCache>
#include <QPainter>
#include <QTransform>
#include <QSet>
#include <QStack>
#include <cmath>

namespace core { namespace paint {

QImage loadBrushAlphaMask(const QString& path)
{
    QImage source(path);
    if (source.isNull()) return QImage();
    source = source.convertToFormat(QImage::Format_ARGB32);

    // Uma imagem realmente transparente usa alpha. Máscaras comuns salvas
    // como JPG/BMP ou PNG opaco usam a luminância para que preto/branco
    // funcione sem configuração adicional.
    bool hasUsefulAlpha = false;
    for (int y = 0; y < source.height() && !hasUsefulAlpha; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            if (qAlpha(row[x]) < 250) { hasUsefulAlpha = true; break; }
        }
    }

    QImage mask(source.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < source.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        uchar* dst = mask.scanLine(y);
        for (int x = 0; x < source.width(); ++x)
            dst[x] = uchar(hasUsefulAlpha ? qAlpha(src[x]) : qGray(src[x]));
    }
    return mask;
}

static qreal alphaMaskSample(const BrushSettings& b, int dx, int dy, int radius)
{
    if (b.alphaMask.isNull()) return 1.0;
    if (radius <= 0) {
        const int mx = b.alphaMask.width() / 2;
        const int my = b.alphaMask.height() / 2;
        const int v = qGray(b.alphaMask.pixel(mx, my));
        return (b.alphaMaskInvert ? 255 - v : v) / 255.0;
    }

    double nx = double(dx) / radius;
    double ny = double(dy) / radius;
    const double rad = -double(b.alphaMaskRotation) * M_PI / 180.0;
    const double cs = std::cos(rad), sn = std::sin(rad);
    const double rx = nx * cs - ny * sn;
    const double ry = nx * sn + ny * cs;
    if (rx < -1.0 || rx > 1.0 || ry < -1.0 || ry > 1.0) return 0.0;

    const int mx = qBound(0, int(std::lround((rx * 0.5 + 0.5) * (b.alphaMask.width()  - 1))),
                          b.alphaMask.width() - 1);
    const int my = qBound(0, int(std::lround((ry * 0.5 + 0.5) * (b.alphaMask.height() - 1))),
                          b.alphaMask.height() - 1);
    int v = qGray(b.alphaMask.pixel(mx, my));
    if (b.alphaMaskInvert) v = 255 - v;
    return v / 255.0;
}

QVector<BrushSample> brushSamples(const BrushSettings& b)
{
    QVector<BrushSample> out;
    const int size = qMax(1, b.size);
    if (size == 1) {
        qreal strength = b.usesAlphaMask() ? alphaMaskSample(b, 0, 0, 0) : 1.0;
        if (strength > 0.001) out.push_back({QPoint(0, 0), strength});
        return out;
    }

    const int r = size - 1;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            qreal strength = 1.0;
            if (b.shape == QLatin1String("circle")) {
                if (dx * dx + dy * dy > r * r + r * 0.5) continue;
            } else if (b.shape == QLatin1String("diamond")) {
                if (qAbs(dx) + qAbs(dy) > r) continue;
            } else if (b.shape == QLatin1String("alpha")) {
                if (b.alphaMask.isNull()) continue;
                strength = alphaMaskSample(b, dx, dy, r);
                if (strength <= 0.001) continue;
            }
            out.push_back({QPoint(dx, dy), strength});
        }
    }
    return out;
}

QVector<QPoint> brushOffsets(const BrushSettings& b)
{
    QVector<QPoint> out;
    const QVector<BrushSample> samples = brushSamples(b);
    out.reserve(samples.size());
    for (const BrushSample& sample : samples) out.push_back(sample.point);
    return out;
}

QImage loadRasterBrushTip(const QString& path)
{
    if (path.trimmed().isEmpty()) return QImage();
    QImage image(path);
    if (image.isNull()) return QImage();
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

int rasterBrushFootprintPx(const RasterBrushSettings& b)
{
    if (!b.pixelArt()) return qMax(1, b.sizePx);
    const int scale = qBound(1, b.pixelScale, 8);
    // Mantém o mesmo teto físico histórico do brush normal para evitar
    // alocações gigantes quando tamanho + escala são combinados.
    return qBound(1, qMax(1, b.pixelSize) * scale, 2048);
}

int rasterBrushSpacingPx(const RasterBrushSettings& b)
{
    if (b.pixelArt()) return qBound(1, b.pixelScale, 8);
    return qMax(1, qRound(qMax(1, b.sizePx) * qBound(1, b.spacingPercent, 400) / 100.0));
}

QPointF rasterBrushSnapPoint(const RasterBrushSettings& b, const QPointF& localPoint)
{
    if (!b.pixelArt()) return localPoint;
    const int scale = qBound(1, b.pixelScale, 8);
    const int cellX = int(std::floor(localPoint.x() / scale));
    const int cellY = int(std::floor(localPoint.y() / scale));
    return QPointF(cellX * scale + scale * 0.5,
                   cellY * scale + scale * 0.5);
}

QRectF rasterBrushTargetRect(const RasterBrushSettings& b, const QPointF& localCenter)
{
    if (!b.pixelArt()) {
        const int size = qMax(1, b.sizePx);
        return QRectF(localCenter.x() - size / 2.0, localCenter.y() - size / 2.0, size, size);
    }
    const int scale = qBound(1, b.pixelScale, 8);
    const int logicalSize = qMax(1, qMin(b.pixelSize, 2048 / scale));
    const QPointF snapped = rasterBrushSnapPoint(b, localCenter);
    const int cellX = int(std::floor(snapped.x() / scale));
    const int cellY = int(std::floor(snapped.y() / scale));
    const int leftCell = cellX - logicalSize / 2;
    const int topCell = cellY - logicalSize / 2;
    return QRectF(leftCell * scale, topCell * scale,
                  logicalSize * scale, logicalSize * scale);
}

QVector<QPointF> rasterBrushPixelLine(const RasterBrushSettings& b,
                                      const QPointF& from, const QPointF& to)
{
    QVector<QPointF> out;
    if (!b.pixelArt()) return out;
    const int scale = qBound(1, b.pixelScale, 8);
    const QPointF a = rasterBrushSnapPoint(b, from);
    const QPointF z = rasterBrushSnapPoint(b, to);
    int x0 = int(std::floor(a.x() / scale));
    int y0 = int(std::floor(a.y() / scale));
    const int x1 = int(std::floor(z.x() / scale));
    const int y1 = int(std::floor(z.y() / scale));
    const int dx = qAbs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -qAbs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    bool first = true;
    while (true) {
        if (!first) out.push_back(QPointF(x0 * scale + scale * 0.5,
                                         y0 * scale + scale * 0.5));
        first = false;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return out;
}

bool rasterBrushPixelPerfectSkipMiddle(const RasterBrushSettings& b,
                                       const QPointF& previous,
                                       const QPointF& middle,
                                       const QPointF& next)
{
    if (!b.pixelPerfectActive()) return false;

    const int scale = qBound(1, b.pixelScale, 8);
    const auto logicalCell = [&b, scale](const QPointF& point) {
        const QPointF snapped = rasterBrushSnapPoint(b, point);
        return QPoint(int(std::floor(snapped.x() / scale)),
                      int(std::floor(snapped.y() / scale)));
    };

    const QPoint a = logicalCell(previous);
    const QPoint m = logicalCell(middle);
    const QPoint c = logicalCell(next);
    if (a == m || m == c || a == c) return false;

    // A-M-C ocupam três cantos de um bloco 2x2. A e C já se tocam
    // diagonalmente; manter M cria o "double pixel" visual.
    const QPoint ac = c - a;
    if (qAbs(ac.x()) != 1 || qAbs(ac.y()) != 1) return false;

    const QPoint am = m - a;
    const QPoint mc = c - m;
    const bool firstCardinal = qAbs(am.x()) + qAbs(am.y()) == 1;
    const bool secondCardinal = qAbs(mc.x()) + qAbs(mc.y()) == 1;
    return firstCardinal && secondCardinal;
}

static QPainter::CompositionMode rasterBlendMode(const QString& id)
{
    if (id == QLatin1String("multiply")) return QPainter::CompositionMode_Multiply;
    if (id == QLatin1String("screen")) return QPainter::CompositionMode_Screen;
    if (id == QLatin1String("overlay")) return QPainter::CompositionMode_Overlay;
    if (id == QLatin1String("darken")) return QPainter::CompositionMode_Darken;
    if (id == QLatin1String("lighten")) return QPainter::CompositionMode_Lighten;
    return QPainter::CompositionMode_SourceOver;
}

static QImage proceduralRoundTip(int size, int hardness, const QColor& color, qreal flow)
{
    QImage dab(size, size, QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);
    const double radius = size / 2.0;
    const double inner = radius * qBound(0, hardness, 100) / 100.0;
    const double denom = qMax(0.001, radius - inner);
    const double cx = (size - 1) / 2.0, cy = (size - 1) / 2.0;
    for (int y = 0; y < size; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
        for (int x = 0; x < size; ++x) {
            const double dx = x - cx, dy = y - cy;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d > radius) { row[x] = 0; continue; }
            double a = 1.0;
            if (d > inner) a = qBound(0.0, 1.0 - (d - inner) / denom, 1.0);
            a *= flow;
            const int alpha = qBound(0, qRound(a * color.alpha()), 255);
            row[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), alpha));
        }
    }
    return dab;
}

static QImage proceduralPixelTip(int logicalSize, const RasterBrushSettings& b, qreal flow)
{
    logicalSize = qMax(1, logicalSize);
    QImage dab(logicalSize, logicalSize, QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);
    const double center = (logicalSize - 1) * 0.5;
    const double radius = qMax(0.5, logicalSize * 0.5);
    const int alpha = qBound(0, qRound(flow * b.color.alpha()), 255);
    const QRgb px = qPremultiply(qRgba(b.color.red(), b.color.green(), b.color.blue(), alpha));
    for (int y = 0; y < logicalSize; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
        for (int x = 0; x < logicalSize; ++x) {
            bool covered = true;
            if (b.pixelShape == QLatin1String("circle")) {
                const double dx = x - center, dy = y - center;
                covered = dx * dx + dy * dy <= radius * radius;
            }
            if (covered) row[x] = px;
        }
    }
    return dab;
}

static bool pixelDitherKeep(const QString& mode, int gx, int gy)
{
    if (mode == QLatin1String("25")) return (gx & 1) == 0 && (gy & 1) == 0;
    if (mode == QLatin1String("50")) return ((gx + gy) & 1) == 0;
    if (mode == QLatin1String("75")) return !((gx & 1) != 0 && (gy & 1) != 0);
    return true;
}

static void applyPixelDither(QImage& dab, const RasterBrushSettings& b, int targetLeft, int targetTop)
{
    if (!b.pixelArt() || b.pixelDither == QLatin1String("none")) return;
    const int scale = qBound(1, b.pixelScale, 8);
    for (int y = 0; y < dab.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
        const int gy = int(std::floor(double(targetTop + y) / scale));
        for (int x = 0; x < dab.width(); ++x) {
            const int gx = int(std::floor(double(targetLeft + x) / scale));
            if (!pixelDitherKeep(b.pixelDither, gx, gy)) row[x] = 0;
        }
    }
}

static bool hasUsefulAlpha(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x]) < 250) return true;
    }
    return false;
}

static quint32 brushNoiseHash(int x, int y, quint32 seed)
{
    quint32 h = seed ^ (quint32(x) * 0x9E3779B9u) ^ (quint32(y) * 0x85EBCA6Bu);
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

static double brushValueNoise(double x, double y, quint32 seed)
{
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const int x1 = x0 + 1, y1 = y0 + 1;
    const auto valueAt = [seed](int ix, int iy) {
        return (brushNoiseHash(ix, iy, seed) & 0x00FFFFFFu) / double(0x00FFFFFFu);
    };
    double tx = x - x0, ty = y - y0;
    tx = tx * tx * (3.0 - 2.0 * tx);
    ty = ty * ty * (3.0 - 2.0 * ty);
    const double a = valueAt(x0, y0) * (1.0 - tx) + valueAt(x1, y0) * tx;
    const double b = valueAt(x0, y1) * (1.0 - tx) + valueAt(x1, y1) * tx;
    return a * (1.0 - ty) + b * ty;
}

static QImage blendRasterTipEdges(QImage source, const RasterBrushSettings& b)
{
    if (!b.softenImageEdges || source.isNull() || b.edgeSoftnessStrength <= 0) return source;
    source = source.convertToFormat(QImage::Format_ARGB32);
    const int w = source.width(), h = source.height();
    if (w <= 0 || h <= 0) return source;

    const int minSide = qMin(w, h);
    const int blendPx = qMax(1, qRound(minSide * qBound(1, b.edgeSoftnessPercent, 50) / 100.0));
    const double strength = qBound(0, b.edgeSoftnessStrength, 100) / 100.0;
    const double irregularity = qBound(0, b.edgeIrregularityPercent, 100) / 100.0;
    const int alphaThreshold = 4;
    const int inf = 1 << 20;

    // Distância aproximada até a transparência mais próxima. Diferente do
    // antigo fade retangular, isto segue o CONTORNO REAL da textura alpha.
    QVector<int> dist(w * h, inf);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (qAlpha(row[x]) <= alphaThreshold) dist[i] = 0;
            else if (x == 0 || y == 0 || x == w - 1 || y == h - 1) dist[i] = 1; // fora do canvas é transparente
        }
    }

    // Chamfer 3/4: O(pixels), suficientemente suave para o contorno e barato
    // para cachear por brush/tamanho/configuração.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            int d = dist[i];
            if (x > 0) d = qMin(d, dist[i - 1] + 3);
            if (y > 0) d = qMin(d, dist[i - w] + 3);
            if (x > 0 && y > 0) d = qMin(d, dist[i - w - 1] + 4);
            if (x + 1 < w && y > 0) d = qMin(d, dist[i - w + 1] + 4);
            dist[i] = d;
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            const int i = y * w + x;
            int d = dist[i];
            if (x + 1 < w) d = qMin(d, dist[i + 1] + 3);
            if (y + 1 < h) d = qMin(d, dist[i + w] + 3);
            if (x + 1 < w && y + 1 < h) d = qMin(d, dist[i + w + 1] + 4);
            if (x > 0 && y + 1 < h) d = qMin(d, dist[i + w - 1] + 4);
            dist[i] = d;
        }
    }

    // As distâncias acima usam 3 unidades por pixel ortogonal.
    const double baseWidth = qMax(1.0, blendPx * 3.0);
    const double noiseScale = qMax(2.0, blendPx * 0.32);
    const quint32 seed = quint32(qulonglong(b.tipImage.cacheKey())) ^ 0xB17D5A3Du;

    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(source.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = row[x];
            const int originalAlpha = qAlpha(px);
            if (originalAlpha <= alphaThreshold) continue;

            // Ruído de baixa frequência altera localmente a largura da faixa,
            // quebrando o contorno sem criar serrilhado/chuvisco por pixel.
            const double n = brushValueNoise(x / noiseScale, y / noiseScale, seed); // 0..1
            const double signedNoise = (n - 0.5) * 2.0;
            const double localWidth = baseWidth * qBound(0.45, 1.0 + signedNoise * irregularity * 0.60, 1.65);
            const double shiftedDistance = qMax(0.0, double(dist[y * w + x]) + signedNoise * irregularity * baseWidth * 0.18);
            double t = qBound(0.0, shiftedDistance / qMax(1.0, localWidth), 1.0);
            t = t * t * (3.0 - 2.0 * t); // smoothstep

            double factor = 1.0 - strength * (1.0 - t);
            if (!b.preserveEdgeCenter) {
                // Opcional: integra levemente também o miolo, sem destruir os
                // detalhes. O efeito continua muito menor que na faixa externa.
                factor *= 1.0 - irregularity * 0.10 * (0.35 + 0.65 * (1.0 - n));
            }

            const int a = qBound(0, qRound(originalAlpha * qBound(0.0, factor, 1.0)), 255);
            row[x] = qRgba(qRed(px), qGreen(px), qBlue(px), a);
        }
    }
    return source;
}

static QImage preparedRasterTipSource(const RasterBrushSettings& b, int size)
{
    if (b.tipImage.isNull()) return QImage();
    static QCache<QString, QImage> cache(128 * 1024); // custo em KiB
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(qulonglong(b.tipImage.cacheKey())).arg(size).arg(b.pixelArt() ? 1 : 0)
        .arg(b.softenImageEdges ? 1 : 0).arg(b.edgeSoftnessPercent).arg(b.edgeSoftnessStrength)
        .arg(b.edgeIrregularityPercent).arg(b.preserveEdgeCenter ? 1 : 0);
    if (QImage* hit = cache.object(key)) return *hit;

    QImage source = b.tipImage.convertToFormat(QImage::Format_ARGB32);
    source = source.scaled(size, size, Qt::IgnoreAspectRatio,
                           b.pixelArt() ? Qt::FastTransformation : Qt::SmoothTransformation);
    if (!b.pixelArt()) source = blendRasterTipEdges(source, b);
    const int costKb = qMax(1, int(qMin<qint64>(source.sizeInBytes() / 1024, 32 * 1024)));
    cache.insert(key, new QImage(source), costKb);
    return source;
}

static QImage rasterTipImage(const RasterBrushSettings& b, int size, double rotation)
{
    const qreal flow = qBound(0, b.flow, 100) / 100.0;

    if (b.pixelArt()) {
        const int scale = qBound(1, b.pixelScale, 8);
        const int logicalSize = qMax(1, qMin(size, 2048 / scale));
        QImage logical;
        if (b.tipMode == QLatin1String("round") || b.tipImage.isNull()) {
            logical = proceduralPixelTip(logicalSize, b, flow);
        } else {
            QImage source = preparedRasterTipSource(b, logicalSize);
            logical = QImage(logicalSize, logicalSize, QImage::Format_ARGB32_Premultiplied);
            logical.fill(Qt::transparent);
            if (b.tipMode == QLatin1String("color")) {
                QPainter painter(&logical);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                painter.setOpacity(flow);
                painter.drawImage(QPoint(0, 0), source);
                painter.end();
            } else {
                const bool alpha = hasUsefulAlpha(source);
                for (int y = 0; y < logicalSize; ++y) {
                    const QRgb* src = reinterpret_cast<const QRgb*>(source.constScanLine(y));
                    QRgb* dst = reinterpret_cast<QRgb*>(logical.scanLine(y));
                    for (int x = 0; x < logicalSize; ++x) {
                        const int mask = alpha ? qAlpha(src[x]) : qGray(src[x]);
                        const int a = qBound(0, qRound(mask * flow * b.color.alpha() / 255.0), 255);
                        dst[x] = qPremultiply(qRgba(b.color.red(), b.color.green(), b.color.blue(), a));
                    }
                }
            }
        }
        if (b.pixelMirrorH || b.pixelMirrorV) logical = logical.mirrored(b.pixelMirrorH, b.pixelMirrorV);
        if (scale == 1) return logical;
        return logical.scaled(logical.width() * scale, logical.height() * scale,
                              Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    if (b.tipMode == QLatin1String("round") || b.tipImage.isNull())
        return proceduralRoundTip(size, b.hardness, b.color, flow);

    QImage source = preparedRasterTipSource(b, size);
    QImage dab(size, size, QImage::Format_ARGB32_Premultiplied);
    dab.fill(Qt::transparent);

    if (b.tipMode == QLatin1String("color")) {
        QPainter painter(&dab);
        painter.setOpacity(flow);
        painter.drawImage(QPoint(0, 0), source);
        painter.end();
    } else {
        const bool alpha = hasUsefulAlpha(source);
        for (int y = 0; y < size; ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(source.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < size; ++x) {
                const int mask = alpha ? qAlpha(src[x]) : qGray(src[x]);
                const int a = qBound(0, qRound(mask * flow * b.color.alpha() / 255.0), 255);
                dst[x] = qPremultiply(qRgba(b.color.red(), b.color.green(), b.color.blue(), a));
            }
        }
    }

    if (std::abs(rotation) > 0.001) {
        QTransform transform;
        transform.rotate(rotation);
        dab = dab.transformed(transform, Qt::SmoothTransformation);
    }
    return dab;
}

QImage rasterBrushPreviewTip(const RasterBrushSettings& b, double strokeAngleDeg)
{
    const int size = b.pixelArt() ? qMax(1, b.pixelSize) : qMax(1, b.sizePx);
    double rotation = b.pixelArt() ? 0.0 : b.rotation;
    if (!b.pixelArt() && b.rotateToStroke) rotation += strokeAngleDeg;
    // O preview não aplica jitter/scatter aleatório: a silhueta precisa ser
    // estável enquanto o cursor está parado, como em editores de imagem.
    return rasterTipImage(b, size, rotation);
}

QImage rasterBrushPreviewTipAt(const RasterBrushSettings& b, const QPointF& localCenter,
                               double strokeAngleDeg)
{
    QImage tip = rasterBrushPreviewTip(b, strokeAngleDeg);
    if (tip.isNull() || !b.pixelArt()) return tip;
    const QRectF target = rasterBrushTargetRect(b, localCenter);
    applyPixelDither(tip, b, qRound(target.left()), qRound(target.top()));
    return tip;
}

QRectF rasterBrushDab(QImage* targetImage, const RasterBrushSettings& b,
                      const QPointF& localCenter, bool erase, double strokeAngleDeg,
                      bool alphaMaskMode, bool alphaLock, const QRegion* clipRegion)
{
    if (!targetImage || targetImage->isNull()) return QRectF();

    const bool pixel = b.pixelArt();
    QRandomGenerator* rng = QRandomGenerator::global();
    double sizeFactor = 1.0;
    if (!pixel && b.sizeJitter > 0)
        sizeFactor -= rng->generateDouble() * qBound(0, b.sizeJitter, 100) / 100.0;
    const int size = pixel ? qMax(1, b.pixelSize)
                           : qMax(1, qRound(qMax(1, b.sizePx) * qMax(0.05, sizeFactor)));

    double rotation = pixel ? 0.0 : b.rotation;
    if (!pixel && b.rotateToStroke) rotation += strokeAngleDeg;
    if (!pixel && b.rotationJitter > 0)
        rotation += (rng->generateDouble() * 2.0 - 1.0) * qBound(0, b.rotationJitter, 360);

    QPointF center = pixel ? rasterBrushSnapPoint(b, localCenter) : localCenter;
    if (!pixel && b.scatterPercent > 0) {
        const double amount = size * qBound(0, b.scatterPercent, 400) / 100.0;
        const double offset = (rng->generateDouble() * 2.0 - 1.0) * amount;
        constexpr double kPi = 3.14159265358979323846;
        const double rad = (strokeAngleDeg + 90.0) * kPi / 180.0;
        center += QPointF(std::cos(rad) * offset, std::sin(rad) * offset);
    }

    QImage dab = rasterTipImage(b, size, rotation);
    if (dab.isNull()) return QRectF();
    QRectF target = pixel ? rasterBrushTargetRect(b, center)
                          : QRectF(center.x() - dab.width() / 2.0,
                                   center.y() - dab.height() / 2.0,
                                   dab.width(), dab.height());
    if (pixel) applyPixelDither(dab, b, qRound(target.left()), qRound(target.top()));

    const QRect affected = target.toAlignedRect().intersected(targetImage->rect());
    // Com Alpha Lock, a borracha não pode alterar transparência; portanto ela
    // não modifica o conteúdo. Para esconder visualmente, use a máscara raster.
    if (alphaLock && erase && !alphaMaskMode) return target;
    QImage alphaBefore;
    QImage replaceBefore;
    if (alphaLock && !alphaMaskMode && !affected.isEmpty())
        alphaBefore = targetImage->copy(affected).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (pixel && b.pixelReplaceEnabled && !affected.isEmpty())
        replaceBefore = targetImage->copy(affected).convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter painter(targetImage);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !pixel);
    if (clipRegion) painter.setClipRegion(*clipRegion, Qt::IntersectClip);
    painter.setOpacity(qBound(0, b.opacity, 100) / 100.0);
    if (alphaMaskMode) {
        // Máscara de camada real: a luminância do brush define a máscara.
        // Branco revela, preto esconde e cinza produz transparência parcial.
        // A alpha do dab continua sendo a cobertura (Flow/Hardness/tip PNG).
        const QColor sourceColor = erase ? QColor(0, 0, 0, 255) : b.color;
        const int value = qGray(sourceColor.rgb());
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QImage applied(dab.size(), QImage::Format_ARGB32_Premultiplied);
        applied.fill(Qt::transparent);
        for (int y = 0; y < dab.height(); ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(dab.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(applied.scanLine(y));
            for (int x = 0; x < dab.width(); ++x) {
                const int coverage = qAlpha(src[x]);
                dst[x] = qPremultiply(qRgba(value, value, value, coverage));
            }
        }
        painter.drawImage(target.topLeft(), applied);
    } else if (erase) {
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.drawImage(target.topLeft(), dab);
    } else {
        painter.setCompositionMode(rasterBlendMode(b.blendMode));
        painter.drawImage(target.topLeft(), dab);
    }
    painter.end();

    // Color Replace de Pixel Art é exato: pixels que não tinham a cor-alvo
    // antes do dab voltam ao valor original. Assim a função também respeita
    // alpha, máscaras e qualquer blend sem criar uma segunda rota de pintura.
    if (pixel && b.pixelReplaceEnabled && !replaceBefore.isNull()) {
        const QColor wanted = b.pixelReplaceColor;
        for (int y = 0; y < affected.height(); ++y) {
            const QRgb* beforeRow = reinterpret_cast<const QRgb*>(replaceBefore.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(targetImage->scanLine(affected.y() + y)) + affected.x();
            for (int x = 0; x < affected.width(); ++x) {
                const QRgb before = qUnpremultiply(beforeRow[x]);
                const bool match = qRed(before) == wanted.red() && qGreen(before) == wanted.green() &&
                                   qBlue(before) == wanted.blue() && qAlpha(before) == wanted.alpha();
                if (!match) dst[x] = beforeRow[x];
            }
        }
    }

    // Alpha Lock real: a pintura altera RGB, mas o canal alpha original fica
    // exatamente igual. Assim pixels transparentes continuam intocados e
    // bordas semi-transparentes não engrossam nem afinam.
    if (alphaLock && !alphaMaskMode && !alphaBefore.isNull()) {
        for (int y = 0; y < affected.height(); ++y) {
            const QRgb* srcAlpha = reinterpret_cast<const QRgb*>(alphaBefore.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(targetImage->scanLine(affected.y() + y)) + affected.x();
            for (int x = 0; x < affected.width(); ++x) {
                const int a = qAlpha(srcAlpha[x]);
                if (a <= 0) { dst[x] = 0; continue; }
                const QRgb unpremult = qUnpremultiply(dst[x]);
                dst[x] = qPremultiply(qRgba(qRed(unpremult), qGreen(unpremult), qBlue(unpremult), a));
            }
        }
    }
    return target;
}

QRectF rasterBrushDab(const LayerPtr& layer, const RasterBrushSettings& b,
                      const QPointF& localCenter, bool erase, double strokeAngleDeg)
{
    if (!layer || layer->type != LayerType::Image || layer->image.isNull())
        return QRectF();
    const QRectF rect = rasterBrushDab(&layer->image, b, localCenter, erase, strokeAngleDeg,
                                       false, layer->alphaLock, nullptr);
    layer->imagewidth = layer->image.width();
    layer->imageheight = layer->image.height();
    return rect;
}

QImage slopeImage(const QImage& source, SlopeAxis axis, double step)
{
    if (source.isNull()) return QImage();
    const QImage src = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    if (qFuzzyIsNull(step)) return src.copy();

    const int w = src.width();
    const int h = src.height();
    if (axis == SlopeAxis::Horizontal) {
        for (int y = 0; y < h; ++y) {
            const int shift = qRound(step * y);
            const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int nx = x + shift;
                if (nx >= 0 && nx < w) dst[nx] = in[x];
            }
        }
    } else {
        for (int x = 0; x < w; ++x) {
            const int shift = qRound(step * x);
            for (int y = 0; y < h; ++y) {
                const int ny = y + shift;
                if (ny < 0 || ny >= h) continue;
                const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
                QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(ny));
                dst[x] = in[x];
            }
        }
    }
    return out;
}

Stamp transformStamp(const Stamp& s, const BrushSettings& b)
{
    if (!b.flipH && !b.flipV) return s;
    Stamp out;
    out.w = s.w; out.h = s.h;
    for (int i = 0; i < s.tiles.size(); ++i) {
        QPoint o = s.offsets[i];
        if (b.flipH) o.setX(s.w - 1 - o.x());
        if (b.flipV) o.setY(s.h - 1 - o.y());
        out.tiles.push_back(s.tiles[i]);
        out.offsets.push_back(o);
    }
    return out;
}

/// Escreve um tile numa celula respeitando o modo "colocar por cima".
static void putTile(Editor& ed, const LayerPtr& layer, int x, int y, const TileRef& t)
{
    if (!layer->inBounds(x, y)) return;
    if (isBlankTile(ed, t)) { if (!(ed.session.placeOnTop || ed.session.heldStack)) layer->data2D[y][x].clear(); return; }
    if (ed.session.placeOnTop || ed.session.heldStack) {
        Cell c = layer->data2D[y][x];
        c.push_back(t);
        layer->data2D[y][x] = c;
    } else {
        Cell c; c.push_back(t);
        layer->data2D[y][x] = c;
    }
}

static void putStack(Editor& ed, const LayerPtr& layer, int x, int y, const Cell& stack)
{
    if (!layer->inBounds(x, y) || stack.isEmpty()) return;
    bool blank = true;
    for (const TileRef& tile : stack) if (!isBlankTile(ed,tile)) { blank=false; break; }
    if (blank) { if (!(ed.session.placeOnTop || ed.session.heldStack)) layer->data2D[y][x].clear(); return; }
    if (ed.session.placeOnTop || ed.session.heldStack) {
        Cell dst = layer->data2D[y][x];
        dst += stack;
        layer->data2D[y][x] = dst;
    } else {
        layer->data2D[y][x] = stack;
    }
}

static void popTile(Editor& ed, const LayerPtr& layer, int x, int y)
{
    if (!layer->inBounds(x, y)) return;
    if (ed.session.placeOnTop || ed.session.heldStack) {
        Cell c = layer->data2D[y][x];
        if (!c.isEmpty()) { c.removeLast(); layer->data2D[y][x] = c; }
    } else {
        layer->data2D[y][x] = Cell();
    }
}

bool randomScatterPass(const Editor& ed)
{
    if (!ed.session.randomMode || !ed.session.randomScattering) return true;
    const int chance = clampi(ed.session.randomScatterPercent, 0, 100);
    if (chance <= 0) return false;
    if (chance >= 100) return true;
    return int(QRandomGenerator::global()->bounded(100)) < chance;
}

void stampAt(Editor& ed, const LayerPtr& layer, int gx, int gy)
{
    if (!layer || layer->type != LayerType::Tile) return;

    if (ed.session.randomMode && !ed.randomPool.isEmpty()) {
        if (!randomScatterPass(ed)) return;
        const RandomEntry* e = ed.pickRandomEntry();
        if (!e) return;
        for (int i = 0; i < e->tiles.size(); ++i) {
            const QPoint o = e->offsets[i];
            putTile(ed, layer, gx + o.x(), gy + o.y(), e->tiles[i]);
        }
        return;
    }
    const Stamp stamp = transformStamp(ed.currentStamp(), ed.session.brush);
    if (!stamp.valid()) return;
    QHash<quint64, Cell> stacks;
    QHash<quint64, QPoint> points;
    const int count = qMin(stamp.tiles.size(), stamp.offsets.size());
    for (int i = 0; i < count; ++i) {
        const QPoint o = stamp.offsets[i];
        const quint64 key = (quint64(quint32(o.y())) << 32) | quint64(quint32(o.x()));
        stacks[key].push_back(stamp.tiles[i]);
        points.insert(key, o);
    }
    for (auto it = stacks.cbegin(); it != stacks.cend(); ++it) {
        const QPoint o = points.value(it.key());
        putStack(ed, layer, gx + o.x(), gy + o.y(), it.value());
    }
}

void eraseAt(Editor& ed, const LayerPtr& layer, int gx, int gy)
{
    if (!layer || layer->type != LayerType::Tile) return;
    popTile(ed, layer, gx, gy);
}

void fillAt(Editor& ed, const LayerPtr& layer, int gx, int gy)
{
    if (!layer || layer->type != LayerType::Tile || !layer->inBounds(gx, gy)) return;
    const Stamp stamp = ed.currentStamp();
    if (!stamp.valid()) return;

    const TileRef top = layer->topAt(gx, gy);
    const QString targetKey = top.isValid() ? top.key() : QStringLiteral("null");
    // Preserva metadados Wang quando o stamp veio do mapa (seleção por
    // botão direito). O MapView retila o Terrain ao final do flood fill,
    // portanto copiar um Autotile não degrada a variante para tile estático.
    const TileRef newCell = stamp.tiles.first();
    if (targetKey == newCell.key() && !(ed.session.placeOnTop || ed.session.heldStack)) return;

    QStack<QPoint> stack;
    QSet<qint64> visited;
    stack.push(QPoint(gx, gy));
    while (!stack.isEmpty()) {
        const QPoint p = stack.pop();
        const qint64 key = qint64(p.y()) * 100000 + p.x();
        if (visited.contains(key)) continue;
        visited.insert(key);
        if (!layer->inBounds(p.x(), p.y())) continue;
        const TileRef cur = layer->topAt(p.x(), p.y());
        const QString ck = cur.isValid() ? cur.key() : QStringLiteral("null");
        if (ck != targetKey) continue;
        putTile(ed, layer, p.x(), p.y(), newCell);
        stack.push(QPoint(p.x() + 1, p.y()));
        stack.push(QPoint(p.x() - 1, p.y()));
        stack.push(QPoint(p.x(), p.y() + 1));
        stack.push(QPoint(p.x(), p.y() - 1));
    }
}

void rectFill(Editor& ed, const LayerPtr& layer, int x0, int y0, int x1, int y1,
              bool hollow, bool erase)
{
    if (!layer || layer->type != LayerType::Tile) return;
    const bool useRandom = !erase && ed.session.randomMode && !ed.randomPool.isEmpty();
    const Stamp stamp = (useRandom || erase) ? Stamp{ 0, 0, {}, {} }
                                             : transformStamp(ed.currentStamp(), ed.session.brush);
    const int minX = qMax(0, qMin(x0, x1)), maxX = qMin(layer->cols - 1, qMax(x0, x1));
    const int minY = qMax(0, qMin(y0, y1)), maxY = qMin(layer->rows - 1, qMax(y0, y1));
    if (minX > maxX || minY > maxY) return;

    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            if (hollow && x != qMin(x0,x1) && x != qMax(x0,x1) && y != qMin(y0,y1) && y != qMax(y0,y1)) continue;
            if (erase) { popTile(ed, layer, x, y); continue; }
            if (useRandom) {
                if (!randomScatterPass(ed)) continue;
                const TileRef t = ed.pickRandomSingleTile();
                if (t.isValid()) putTile(ed, layer, x, y, t);
                continue;
            }
            if (!stamp.valid()) continue;
            const int rx = ((x - qMin(x0,x1)) % stamp.w + stamp.w) % stamp.w;
            const int ry = ((y - qMin(y0,y1)) % stamp.h + stamp.h) % stamp.h;
            const Cell stack = stamp.stackAt(rx, ry);
            if (!stack.isEmpty()) putStack(ed, layer, x, y, stack);
        }
}

void circleFill(Editor& ed, const LayerPtr& layer, int x0, int y0, int x1, int y1,
                bool hollow, bool erase)
{
    if (!layer || layer->type != LayerType::Tile) return;
    const bool useRandom = !erase && ed.session.randomMode && !ed.randomPool.isEmpty();
    const Stamp stamp = (useRandom || erase) ? Stamp{ 0, 0, {}, {} }
                                             : transformStamp(ed.currentStamp(), ed.session.brush);
    const int minX = qMax(0, qMin(x0, x1)), maxX = qMin(layer->cols - 1, qMax(x0, x1));
    const int minY = qMax(0, qMin(y0, y1)), maxY = qMin(layer->rows - 1, qMax(y0, y1));
    if (minX > maxX || minY > maxY) return;

    const double cx = (double(x0) + x1) / 2.0, cy = (double(y0) + y1) / 2.0;
    double rx = (std::abs(double(x1)-x0) + 1) / 2.0, ry = (std::abs(double(y1)-y0) + 1) / 2.0;
    if (rx < 0.5) rx = 0.5;
    if (ry < 0.5) ry = 0.5;
    auto inside = [&](int x, int y) {
        const double dx = (x - cx) / rx, dy = (y - cy) / ry;
        return dx * dx + dy * dy <= 1.02;
    };

    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            if (!inside(x, y)) continue;
            if (hollow) {
                const bool n1 = inside(x - 1, y);
                const bool n2 = inside(x + 1, y);
                const bool n3 = inside(x, y - 1);
                const bool n4 = inside(x, y + 1);
                if (n1 && n2 && n3 && n4) continue;
            }
            if (erase) { popTile(ed, layer, x, y); continue; }
            if (useRandom) {
                if (!randomScatterPass(ed)) continue;
                const TileRef t = ed.pickRandomSingleTile();
                if (t.isValid()) putTile(ed, layer, x, y, t);
                continue;
            }
            if (!stamp.valid()) continue;
            const int rxP = ((x - qMin(x0,x1)) % stamp.w + stamp.w) % stamp.w;
            const int ryP = ((y - qMin(y0,y1)) % stamp.h + stamp.h) % stamp.h;
            const Cell stack = stamp.stackAt(rxP, ryP);
            if (!stack.isEmpty()) putStack(ed, layer, x, y, stack);
        }
}

QVector<QPoint> linePoints(int x0, int y0, int x1, int y1)
{
    QVector<QPoint> pts;
    int dx = qAbs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -qAbs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        pts.push_back(QPoint(x0, y0));
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return pts;
}

void lineStroke(Editor& ed, const LayerPtr& layer, int x0, int y0, int x1, int y1, bool erase)
{
    if (!layer || layer->type != LayerType::Tile) return;
    const QVector<QPoint> pts = linePoints(x0, y0, x1, y1);
    const int spacing = qMax(1, ed.session.brush.spacing);
    QSet<quint64> scatterVisited;
    QSet<quint64>* visited = (!erase && ed.session.randomMode && ed.session.randomScattering && !ed.session.randomGridFree)
        ? &scatterVisited : nullptr;
    for (int i = 0; i < pts.size(); ++i) {
        if (i % spacing) continue;
        if (erase) eraseAt(ed, layer, pts[i].x(), pts[i].y());
        else       brushStroke(ed, layer, pts[i].x(), pts[i].y(), false, visited);
    }
}

void brushStroke(Editor& ed, const LayerPtr& layer, int gx, int gy, bool erase,
                 QSet<quint64>* scatterVisited)
{
    if (!layer || layer->type != LayerType::Tile) return;
    const QVector<BrushSample> samples = brushSamples(ed.session.brush);
    const int density = clampi(ed.session.brush.density, 0, 100);
    const bool oneScatterDecision = !erase && scatterVisited &&
                                    ed.session.randomMode && ed.session.randomScattering &&
                                    !ed.session.randomGridFree && !ed.randomPool.isEmpty();
    for (const BrushSample& sample : samples) {
        const QPoint& o = sample.point;
        const int x = gx + o.x(), y = gy + o.y();
        if (oneScatterDecision) {
            const quint64 key = (quint64(quint32(y)) << 32) | quint64(quint32(x));
            if (scatterVisited->contains(key)) continue;
            scatterVisited->insert(key);
        }
        const int effectiveDensity = clampi(int(std::lround(density * sample.strength)), 0, 100);
        if (effectiveDensity <= 0) continue;
        if (effectiveDensity < 100 &&
            int(QRandomGenerator::global()->bounded(100)) >= effectiveDensity) continue;
        if (erase) eraseAt(ed, layer, x, y);
        else       stampAt(ed, layer, x, y);
    }
}

bool pipetteAt(Editor& ed, const LayerPtr& layer, int px, int py)
{
    if (!layer) return false;
    TileRef picked;
    if (layer->type == LayerType::Tile) {
        const double localX = px - layer->offsetx;
        const double localY = py - layer->offsety;
        const int gx = int(std::floor(localX / qMax(1, layer->tileWidth)));
        const int gy = int(std::floor(localY / qMax(1, layer->tileHeight)));
        if (!layer->inBounds(gx, gy)) return false;
        picked = layer->topAt(gx, gy);
    } else if (layer->type == LayerType::Object) {
        MapObject* o = hitObject(layer, px, py);
        if (!o || o->tiles.isEmpty()) return false;
        picked = o->tiles.first();
    } else {
        return false;
    }
    if (!picked.isValid()) return false;
    ed.session.customStamp.clear();
    ed.session.tsSel = TilesetSelection{ picked.tilesetIdx, picked.tx, picked.ty, 1, 1 };
    ed.session.activeTilesetIdx = picked.tilesetIdx;
    emit ed.selectionChanged();
    emit ed.tilesetsChanged();
    return true;
}

CustomStamp stampFromMap(const LayerPtr& layer, int x0, int y0, int x1, int y1)
{
    CustomStamp s;
    if (!layer || layer->type != LayerType::Tile) return s;
    const int minX = qMax(0, qMin(x0, x1)), maxX = qMin(layer->cols - 1, qMax(x0, x1));
    const int minY = qMax(0, qMin(y0, y1)), maxY = qMin(layer->rows - 1, qMax(y0, y1));
    if (minX > maxX || minY > maxY) return s;
    s.w = maxX - minX + 1;
    s.h = maxY - minY + 1;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const Cell cell = layer->cellAt(x, y);
            if (cell.isEmpty()) continue;
            for (const TileRef& t : cell) {
                if (!t.isValid()) continue;
                s.tiles.push_back(t);
                s.offsets.push_back(QPoint(x - minX, y - minY));
            }
        }
    if (s.tiles.isEmpty()) s.clear();
    return s;
}

// ------------------------------------------------------------------ objetos
namespace {

QPointF rotateAround(const QPointF& point, const QPointF& center, double degrees)
{
    if (std::abs(degrees) < 0.000001) return point;
    constexpr double kPi = 3.14159265358979323846;
    const double radians = degrees * kPi / 180.0;
    const double c = std::cos(radians), s = std::sin(radians);
    const double dx = point.x() - center.x();
    const double dy = point.y() - center.y();
    return QPointF(center.x() + dx * c - dy * s,
                   center.y() + dx * s + dy * c);
}

QVector<QPointF> objectHandleCenters(const MapObject& o)
{
    const QPointF center(o.x + o.w / 2.0, o.y + o.h / 2.0);
    const double xs[3] = { o.x, center.x(), o.x + o.w };
    const double ys[3] = { o.y, center.y(), o.y + o.h };
    QVector<QPointF> out;
    out.reserve(8);
    for (int iy = 0; iy < 3; ++iy)
        for (int ix = 0; ix < 3; ++ix) {
            if (ix == 1 && iy == 1) continue;
            out.push_back(rotateAround(QPointF(xs[ix], ys[iy]), center, o.rotation));
        }
    return out;
}

} // namespace

QPolygonF objectPolygon(const MapObject& o)
{
    const QRectF rect = o.rect().normalized();
    const QPointF center = rect.center();
    QPolygonF polygon;
    polygon << rotateAround(rect.topLeft(), center, o.rotation)
            << rotateAround(rect.topRight(), center, o.rotation)
            << rotateAround(rect.bottomRight(), center, o.rotation)
            << rotateAround(rect.bottomLeft(), center, o.rotation);
    return polygon;
}

QRectF objectBounds(const MapObject& o)
{
    return objectPolygon(o).boundingRect();
}

QPointF unrotateObjectPoint(const MapObject& o, const QPointF& p)
{
    return rotateAround(p, o.rect().center(), -o.rotation);
}

MapObject* hitObject(const LayerPtr& layer, double px, double py)
{
    if (!layer || layer->type != LayerType::Object) return nullptr;
    const double localX = px - layer->offsetx;
    const double localY = py - layer->offsety;
    for (int i = layer->objects.size() - 1; i >= 0; --i) {
        MapObject& o = layer->objects[i];
        if (!o.visible) continue;
        const QPointF local = unrotateObjectPoint(o, QPointF(localX, localY));
        if (o.rect().contains(local)) return &o;
    }
    return nullptr;
}

QVector<QRectF> objectHandles(const MapObject& o, double zoom)
{
    // Mantem as alcas praticamente constantes em pixels de tela. A formula
    // antiga impunha 6 px em coordenadas do mapa e fazia as alcas crescerem
    // para 24/48 px na tela em zoom alto.
    const double s = qBound(0.5, 8.0 / qMax(0.05, zoom), 160.0);
    const double h = s / 2.0;
    QVector<QRectF> out;
    for (const QPointF& center : objectHandleCenters(o))
        out.push_back(QRectF(center.x() - h, center.y() - h, s, s));
    return out;
}

int hitHandle(const MapObject& o, double px, double py, double zoom)
{
    const QVector<QRectF> hs = objectHandles(o, zoom);
    for (int i = 0; i < hs.size(); ++i) if (hs[i].contains(px, py)) return i;
    return -1;
}

QLineF objectRotationStem(const MapObject& o, double zoom)
{
    const QPointF center = o.rect().center();
    const QPointF top = rotateAround(QPointF(center.x(), o.y), center, o.rotation);
    const double distance = qBound(10.0, 24.0 / qMax(0.05, zoom), 240.0);
    const QPointF handle = rotateAround(QPointF(center.x(), o.y - distance), center, o.rotation);
    return QLineF(top, handle);
}

QRectF objectRotationHandle(const MapObject& o, double zoom)
{
    const QPointF center = objectRotationStem(o, zoom).p2();
    const double size = qBound(1.0, 10.0 / qMax(0.05, zoom), 200.0);
    return QRectF(center.x() - size / 2.0, center.y() - size / 2.0, size, size);
}

bool hitRotationHandle(const MapObject& o, double px, double py, double zoom)
{
    return objectRotationHandle(o, zoom).adjusted(-2.0 / qMax(0.05, zoom),
                                                   -2.0 / qMax(0.05, zoom),
                                                    2.0 / qMax(0.05, zoom),
                                                    2.0 / qMax(0.05, zoom)).contains(px, py);
}

MapObject makeObjectFromStamp(Editor& ed, double px, double py)
{
    MapObject o;
    const Stamp stamp = ed.currentStamp();
    // O objeto nasce com o tamanho da GRADE DO MAPA, nao do tileset: numa
    // camada de tiles um tile de 16px e desenhado esticado para os 32px da
    // grade, e o objeto tem de sair igual. Usar ts->tilewidth aqui fazia o
    // mesmo tile aparecer com metade do tamanho na camada de objetos.
    const int gw = qMax(1, ed.mapInfo().tileWidth);
    const int gh = qMax(1, ed.mapInfo().tileHeight);
    o.w = stamp.valid() ? stamp.w * gw : gw;
    o.h = stamp.valid() ? stamp.h * gh : gh;
    o.x = px;
    o.y = py;
    if (ed.session.snapObjects || ed.session.heldSnap) {
        const int g = qMax(1, ed.session.snapGridSize);
        o.x = std::floor(o.x / g) * g;
        o.y = std::floor(o.y / g) * g;
    }
    o.tiles = stamp.tiles;
    o.stampW = stamp.w;
    o.stampH = stamp.h;
    o.name = QStringLiteral("Objeto %1").arg(QString::number(QRandomGenerator::global()->bounded(1000)));
    return o;
}

MapObject makeRandomObjectFromPool(Editor& ed, double px, double py, bool* valid)
{
    if (valid) *valid = false;
    if (!ed.session.randomMode || ed.randomPool.isEmpty() || !randomScatterPass(ed)) return MapObject();
    const RandomEntry* entry = ed.pickRandomEntry();
    if (!entry || entry->tiles.isEmpty()) return MapObject();
    MapObject o;
    const int gw = qMax(1, ed.mapInfo().tileWidth);
    const int gh = qMax(1, ed.mapInfo().tileHeight);
    o.stampW = qMax(1, entry->w);
    o.stampH = qMax(1, entry->h);
    o.w = o.stampW * gw;
    o.h = o.stampH * gh;
    o.tiles = entry->tiles;
    const double jitterX = gw * clampi(ed.session.randomGridFreeJitter, 0, 100) / 100.0;
    const double jitterY = gh * clampi(ed.session.randomGridFreeJitter, 0, 100) / 100.0;
    auto jitter = [](double amount) {
        return amount <= 0.0 ? 0.0 : (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * amount;
    };
    o.x = px + jitter(jitterX);
    o.y = py + jitter(jitterY);
    o.name = QStringLiteral("Scatter %1").arg(QString::number(QRandomGenerator::global()->bounded(100000)));
    if (valid) *valid = true;
    return o;
}


}} // namespace core::paint
