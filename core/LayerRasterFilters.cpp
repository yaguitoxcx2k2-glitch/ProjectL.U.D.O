#include "LayerRasterFilters.h"

#include <QCache>
#include <QPainter>
#include <QObject>
#include <QtGlobal>
#include <cmath>
#include <deque>

namespace core {
namespace {

constexpr double kPi = 3.14159265358979323846;

int imageCostKb(const QImage& image)
{
    const qint64 kb = qMax<qint64>(1, qint64(image.sizeInBytes()) / 1024);
    return int(qMin<qint64>(kb, 64 * 1024));
}

QString filterKey(const QImage& source, const QVector<RasterLayerFilter>& filters, bool maskTarget)
{
    QString key = QStringLiteral("%1:%2:%3x%4")
        .arg(source.cacheKey()).arg(maskTarget ? 1 : 0).arg(source.width()).arg(source.height());
    for (const RasterLayerFilter& f : filters) {
        key += QStringLiteral("|%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16")
            .arg(f.type).arg(f.enabled ? 1 : 0)
            .arg(f.radius, 0, 'f', 3).arg(f.strength, 0, 'f', 4)
            .arg(f.angle, 0, 'f', 3).arg(f.quality)
            .arg(f.amount, 0, 'f', 4).arg(f.scale, 0, 'f', 3).arg(f.seed)
            .arg(f.monochrome ? 1 : 0).arg(f.distance, 0, 'f', 3)
            .arg(f.spread, 0, 'f', 3).arg(f.opacity, 0, 'f', 4)
            .arg(f.color.rgba()).arg(f.id).arg(0);
    }
    return key;
}

QImage blendImages(const QImage& original, const QImage& effected, double strength)
{
    const double t = qBound(0.0, strength, 1.0);
    if (t <= 0.0001 || effected.isNull()) return original;
    if (t >= 0.9999) return effected;
    QImage a = original.convertToFormat(QImage::Format_ARGB32);
    QImage b = effected.convertToFormat(QImage::Format_ARGB32);
    if (a.size() != b.size()) return original;
    for (int y = 0; y < a.height(); ++y) {
        QRgb* dst = reinterpret_cast<QRgb*>(a.scanLine(y));
        const QRgb* src = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const auto mix = [t](int av, int bv) { return qBound(0, int(std::lround(av + (bv - av) * t)), 255); };
            dst[x] = qRgba(mix(qRed(dst[x]), qRed(src[x])),
                           mix(qGreen(dst[x]), qGreen(src[x])),
                           mix(qBlue(dst[x]), qBlue(src[x])),
                           mix(qAlpha(dst[x]), qAlpha(src[x])));
        }
    }
    return a;
}

QImage boxBlurHorizontal(const QImage& input, int radius)
{
    if (radius <= 0 || input.isNull()) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    QImage dst(src.size(), QImage::Format_ARGB32);
    const int w = src.width(), h = src.height(), window = radius * 2 + 1;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(dst.scanLine(y));
        qint64 sr=0, sg=0, sb=0, sa=0;
        for (int k = -radius; k <= radius; ++k) {
            const QRgb p = row[qBound(0, k, w - 1)];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
        }
        for (int x = 0; x < w; ++x) {
            out[x] = qRgba(int(sr / window), int(sg / window), int(sb / window), int(sa / window));
            const QRgb leave = row[qBound(0, x - radius, w - 1)];
            const QRgb enter = row[qBound(0, x + radius + 1, w - 1)];
            sr += qRed(enter) - qRed(leave);
            sg += qGreen(enter) - qGreen(leave);
            sb += qBlue(enter) - qBlue(leave);
            sa += qAlpha(enter) - qAlpha(leave);
        }
    }
    return dst;
}

QImage boxBlurVertical(const QImage& input, int radius)
{
    if (radius <= 0 || input.isNull()) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    QImage dst(src.size(), QImage::Format_ARGB32);
    const int w = src.width(), h = src.height(), window = radius * 2 + 1;
    for (int x = 0; x < w; ++x) {
        qint64 sr=0, sg=0, sb=0, sa=0;
        for (int k = -radius; k <= radius; ++k) {
            const QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(qBound(0, k, h - 1)))[x];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
        }
        for (int y = 0; y < h; ++y) {
            QRgb* out = reinterpret_cast<QRgb*>(dst.scanLine(y));
            out[x] = qRgba(int(sr / window), int(sg / window), int(sb / window), int(sa / window));
            const QRgb leave = reinterpret_cast<const QRgb*>(src.constScanLine(qBound(0, y - radius, h - 1)))[x];
            const QRgb enter = reinterpret_cast<const QRgb*>(src.constScanLine(qBound(0, y + radius + 1, h - 1)))[x];
            sr += qRed(enter) - qRed(leave);
            sg += qGreen(enter) - qGreen(leave);
            sb += qBlue(enter) - qBlue(leave);
            sa += qAlpha(enter) - qAlpha(leave);
        }
    }
    return dst;
}

QImage gaussianApprox(const QImage& input, double radius, int quality)
{
    if (radius <= 0.05 || input.isNull()) return input;
    // Três box blurs aproximam um gaussiano e mantêm custo O(pixels), sem
    // multiplicar o trabalho pelo raio. Qualidade ajusta o número de passadas.
    const int r = qBound(1, int(std::lround(radius * 0.55)), 48);
    const int passes = qBound(1, quality + 2, 4);
    QImage work = input.convertToFormat(QImage::Format_ARGB32);
    for (int i = 0; i < passes; ++i) {
        work = boxBlurHorizontal(work, r);
        work = boxBlurVertical(work, r);
    }
    return work;
}

QImage directionalBlur(const QImage& input, double length, double angleDeg, int quality)
{
    if (input.isNull() || length <= 0.05) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage dst(src.size(), QImage::Format_ARGB32_Premultiplied);
    dst.fill(Qt::transparent);
    const int samples = quality <= 0 ? 5 : (quality == 1 ? 9 : 15);
    const double rad = angleDeg * kPi / 180.0;
    const double dx = std::cos(rad) * length;
    const double dy = std::sin(rad) * length;
    QPainter painter(&dst);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setOpacity(1.0 / samples);
    for (int i = 0; i < samples; ++i) {
        const double t = samples == 1 ? 0.0 : (double(i) / (samples - 1) - 0.5);
        painter.drawImage(QPointF(dx * t, dy * t), src);
    }
    painter.end();
    return dst;
}

quint32 hashNoise(int x, int y, int seed)
{
    quint32 h = quint32(seed) ^ quint32(x * 374761393) ^ quint32(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

int signedNoise(quint32 h)
{
    return int(h & 0xffu) - 128;
}

QImage applyNoise(const QImage& input, const RasterLayerFilter& f, bool maskTarget)
{
    QImage out = input.convertToFormat(QImage::Format_ARGB32);
    const double amount = qBound(0.0, f.amount, 1.0);
    if (amount <= 0.0001) return out;
    const int block = qBound(1, int(std::lround(f.scale)), 64);
    for (int y = 0; y < out.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const int bx = x / block, by = y / block;
            const quint32 h = hashNoise(bx, by, f.seed);
            const int n0 = signedNoise(h);
            const int n1 = f.monochrome ? n0 : signedNoise(hashNoise(bx + 917, by - 131, f.seed + 17));
            const int n2 = f.monochrome ? n0 : signedNoise(hashNoise(bx - 311, by + 761, f.seed + 43));
            const QRgb px = row[x];
            if (maskTarget) {
                const int lum = qGray(px);
                const int delta = int(std::lround(n0 * 2.0 * amount));
                const int v = qBound(0, lum + delta, 255);
                row[x] = qRgba(v, v, v, qAlpha(px));
            } else {
                const auto ch = [amount](int base, int n) {
                    return qBound(0, base + int(std::lround(n * 2.0 * amount)), 255);
                };
                row[x] = qRgba(ch(qRed(px), n0), ch(qGreen(px), n1), ch(qBlue(px), n2), qAlpha(px));
            }
        }
    }
    return out;
}

QImage expandAlphaHorizontal(const QImage& input, int radius)
{
    if (input.isNull() || radius <= 0) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    QImage dst(src.size(), QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(dst.scanLine(y));
        std::deque<int> q;
        int next = 0;
        for (int x = 0; x < w; ++x) {
            const int right = qMin(w - 1, x + radius);
            while (next <= right) {
                const int a = qAlpha(row[next]);
                while (!q.empty() && qAlpha(row[q.back()]) <= a) q.pop_back();
                q.push_back(next++);
            }
            const int left = x - radius;
            while (!q.empty() && q.front() < left) q.pop_front();
            const int a = q.empty() ? 0 : qAlpha(row[q.front()]);
            out[x] = qRgba(255, 255, 255, a);
        }
    }
    return dst;
}

QImage expandAlphaVertical(const QImage& input, int radius)
{
    if (input.isNull() || radius <= 0) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32);
    QImage dst(src.size(), QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    for (int x = 0; x < w; ++x) {
        std::deque<int> q;
        int next = 0;
        for (int y = 0; y < h; ++y) {
            const int bottom = qMin(h - 1, y + radius);
            while (next <= bottom) {
                const QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(next))[x];
                const int a = qAlpha(p);
                while (!q.empty()) {
                    const QRgb back = reinterpret_cast<const QRgb*>(src.constScanLine(q.back()))[x];
                    if (qAlpha(back) > a) break;
                    q.pop_back();
                }
                q.push_back(next++);
            }
            const int top = y - radius;
            while (!q.empty() && q.front() < top) q.pop_front();
            const int a = q.empty() ? 0 : qAlpha(reinterpret_cast<const QRgb*>(src.constScanLine(q.front()))[x]);
            reinterpret_cast<QRgb*>(dst.scanLine(y))[x] = qRgba(255, 255, 255, a);
        }
    }
    return dst;
}

QImage expandAlpha(const QImage& input, double spread)
{
    const int r = qBound(0, int(std::lround(spread)), 32);
    if (r <= 0) return input;
    return expandAlphaVertical(expandAlphaHorizontal(input, r), r);
}

QImage contactShadow(const QImage& input, const RasterLayerFilter& f)
{
    if (input.isNull() || f.opacity <= 0.0001) return input;
    const QImage src = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage silhouette(src.size(), QImage::Format_ARGB32_Premultiplied);
    silhouette.fill(Qt::transparent);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(silhouette.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const int a = qAlpha(in[x]);
            out[x] = qPremultiply(qRgba(255, 255, 255, a));
        }
    }
    const QImage expanded = expandAlpha(silhouette, f.spread);
    const double blurRadius = qBound(0.0, f.radius, 64.0);
    QImage soft = gaussianApprox(expanded, blurRadius, f.quality);
    QImage tinted(soft.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);
    const QColor color = f.color.isValid() ? f.color : QColor(Qt::black);
    const double alphaMul = qBound(0.0, f.opacity, 1.0) * (color.alpha() / 255.0);
    for (int y = 0; y < soft.height(); ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(soft.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(tinted.scanLine(y));
        for (int x = 0; x < soft.width(); ++x) {
            const int a = qBound(0, int(std::lround(qAlpha(in[x]) * alphaMul)), 255);
            out[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
        }
    }

    const double rad = f.angle * kPi / 180.0;
    const QPointF offset(std::cos(rad) * f.distance, std::sin(rad) * f.distance);
    QImage result(src.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage(offset, tinted);
    painter.drawImage(QPointF(0, 0), src);
    painter.end();
    return result;
}

QImage contactShadowOverlay(const QImage& input, const RasterLayerFilter& f)
{
    if (input.isNull() || f.opacity <= 0.0001 || f.strength <= 0.0001) return QImage();
    const QImage src = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage silhouette(src.size(), QImage::Format_ARGB32_Premultiplied);
    silhouette.fill(Qt::transparent);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(silhouette.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const int a = qAlpha(in[x]);
            out[x] = qPremultiply(qRgba(255, 255, 255, a));
        }
    }
    const QImage expanded = expandAlpha(silhouette, f.spread);
    const double blurRadius = qBound(0.0, f.radius, 64.0);
    const QImage soft = gaussianApprox(expanded, blurRadius, f.quality);
    QImage tinted(soft.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);
    const QColor color = f.color.isValid() ? f.color : QColor(Qt::black);
    const double alphaMul = qBound(0.0, f.opacity, 1.0)
                          * qBound(0.0, f.strength, 1.0)
                          * (color.alpha() / 255.0);
    for (int y = 0; y < soft.height(); ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(soft.constScanLine(y));
        QRgb* out = reinterpret_cast<QRgb*>(tinted.scanLine(y));
        for (int x = 0; x < soft.width(); ++x) {
            const int a = qBound(0, int(std::lround(qAlpha(in[x]) * alphaMul)), 255);
            out[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
        }
    }
    const double rad = f.angle * kPi / 180.0;
    const QPointF offset(std::cos(rad) * f.distance, std::sin(rad) * f.distance);
    QImage result(src.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage(offset, tinted);
    painter.end();
    return result;
}

QImage applyOne(const QImage& source, const RasterLayerFilter& f, bool maskTarget)
{
    if (!f.enabled || source.isNull()) return source;
    if (f.type == QLatin1String("gaussianBlur"))
        return blendImages(source, gaussianApprox(source, f.radius, f.quality), f.strength);
    if (f.type == QLatin1String("directionalBlur"))
        return blendImages(source, directionalBlur(source, f.radius, f.angle, f.quality), f.strength);
    if (f.type == QLatin1String("noise"))
        return blendImages(source, applyNoise(source, f, maskTarget), f.strength);
    if (f.type == QLatin1String("contactShadow") && !maskTarget)
        return blendImages(source, contactShadow(source, f), f.strength);
    return source;
}

} // namespace

QString rasterLayerFilterTypeLabel(const QString& type)
{
    if (type == QLatin1String("gaussianBlur")) return QObject::tr("Desfoque suave");
    if (type == QLatin1String("directionalBlur")) return QObject::tr("Desfoque em direção");
    if (type == QLatin1String("noise")) return QObject::tr("Granulação");
    if (type == QLatin1String("contactShadow")) return QObject::tr("Sombra de contato");
    return QObject::tr("Efeito");
}

QString rasterLayerFilterSummary(const RasterLayerFilter& f)
{
    if (f.type == QLatin1String("gaussianBlur"))
        return QObject::tr("%1 px · %2%").arg(f.radius, 0, 'f', 1).arg(int(f.strength * 100));
    if (f.type == QLatin1String("directionalBlur"))
        return QObject::tr("%1 px · %2° · %3%").arg(f.radius, 0, 'f', 1).arg(f.angle, 0, 'f', 0).arg(int(f.strength * 100));
    if (f.type == QLatin1String("noise"))
        return QObject::tr("%1% · grão %2 px").arg(int(f.amount * 100)).arg(f.scale, 0, 'f', 1);
    if (f.type == QLatin1String("contactShadow"))
        return QObject::tr("suavidade %1 px · distância %2 px · %3%").arg(f.radius, 0, 'f', 1).arg(f.distance, 0, 'f', 1).arg(int(f.opacity * 100));
    return QString();
}

bool hasEnabledRasterFilters(const QVector<RasterLayerFilter>& filters)
{
    for (const RasterLayerFilter& f : filters) if (f.enabled) return true;
    return false;
}

QImage filteredRasterCached(const QImage& source,
                            const QVector<RasterLayerFilter>& filters,
                            bool maskTarget)
{
    if (source.isNull() || !hasEnabledRasterFilters(filters)) return source;
    static QCache<QString, QImage> cache(256 * 1024); // custo em KiB ≈ 256 MiB
    const QString key = filterKey(source, filters, maskTarget);
    if (QImage* hit = cache.object(key)) return *hit;

    QImage work = source;
    if (maskTarget) {
        // Canonicaliza máscaras antigas (que podiam guardar força no alpha)
        // para cinza opaco. O valor efetivo permanece luminância*alpha, mas
        // os filtros deixam de multiplicar a transparência duas vezes.
        QImage canonical = source.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < canonical.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(canonical.scanLine(y));
            for (int x = 0; x < canonical.width(); ++x) {
                const int v = (qGray(row[x]) * qAlpha(row[x]) + 127) / 255;
                row[x] = qRgba(v, v, v, 255);
            }
        }
        work = canonical;
    }
    for (const RasterLayerFilter& filter : filters)
        work = applyOne(work, filter, maskTarget);
    cache.insert(key, new QImage(work), imageCostKb(work));
    return work;
}

QImage contactShadowOverlayCached(const QImage& silhouette,
                                  const RasterLayerFilter& filter)
{
    if (silhouette.isNull() || !filter.enabled || filter.type != QLatin1String("contactShadow"))
        return QImage();
    static QCache<QString, QImage> cache(192 * 1024);
    const QVector<RasterLayerFilter> one{filter};
    const QString key = QStringLiteral("contact-only|") + filterKey(silhouette, one, false);
    if (QImage* hit = cache.object(key)) return *hit;
    QImage out = contactShadowOverlay(silhouette, filter);
    if (!out.isNull()) cache.insert(key, new QImage(out), imageCostKb(out));
    return out;
}

QImage maskedRasterCached(const QImage& content, const QImage& mask)
{
    if (content.isNull() || mask.isNull()) return content;
    static QCache<QString, QImage> cache(192 * 1024);
    const QString key = QStringLiteral("%1:%2:%3x%4")
        .arg(content.cacheKey()).arg(mask.cacheKey()).arg(content.width()).arg(content.height());
    if (QImage* hit = cache.object(key)) return *hit;

    QImage base = content.convertToFormat(QImage::Format_ARGB32);
    QImage effectiveMask = mask.convertToFormat(QImage::Format_ARGB32);
    if (effectiveMask.size() != base.size())
        effectiveMask = effectiveMask.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < base.height(); ++y) {
        QRgb* dst = reinterpret_cast<QRgb*>(base.scanLine(y));
        const QRgb* mk = reinterpret_cast<const QRgb*>(effectiveMask.constScanLine(y));
        for (int x = 0; x < base.width(); ++x) {
            const int srcA = qAlpha(dst[x]);
            const int mv = (qGray(mk[x]) * qAlpha(mk[x]) + 127) / 255;
            dst[x] = qRgba(qRed(dst[x]), qGreen(dst[x]), qBlue(dst[x]), (srcA * mv + 127) / 255);
        }
    }
    cache.insert(key, new QImage(base), imageCostKb(base));
    return base;
}

} // namespace core
