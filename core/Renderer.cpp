#include "Renderer.h"
#include "LayerRasterFilters.h"
#include "PaintOps.h"
#include "TilesetOps.h"
#include "LayerTree.h"

#include <QCache>
#include <QPainterPath>
#include <QTransform>
#include <cmath>
#include <limits>

namespace core {

QSize visualLayerFrameSize(const LayerPtr& layer)
{
    if (!layer || layer->type != LayerType::Image || layer->image.isNull()) return QSize();
    if (!layer->parallaxAnimationEnabled) return layer->image.size();
    const int columns = qBound(1, layer->parallaxAnimationColumns, 64);
    const int rows = qBound(1, layer->parallaxAnimationRows, 64);
    return QSize(qMax(1, layer->image.width() / columns),
                 qMax(1, layer->image.height() / rows));
}

QRect visualLayerFrameSourceRect(const LayerPtr& layer, qint64 animationTimeMs)
{
    const QSize frameSize = visualLayerFrameSize(layer);
    if (!layer || frameSize.isEmpty()) return QRect();
    if (!layer->parallaxAnimationEnabled) return QRect(QPoint(0, 0), frameSize);
    const int columns = qBound(1, layer->parallaxAnimationColumns, 64);
    const int rows = qBound(1, layer->parallaxAnimationRows, 64);
    const int count = qBound(1, layer->parallaxAnimationFrames, columns * rows);
    const double fps = qBound(0.1, layer->parallaxAnimationFps, 60.0);
    int sequence = count;
    if (layer->parallaxAnimationPingPong && count > 1) sequence = count * 2 - 2;
    int position = sequence <= 1 ? 0 : int(std::floor(qMax<qint64>(0, animationTimeMs) * fps / 1000.0)) % sequence;
    if (layer->parallaxAnimationPingPong && position >= count) position = sequence - position;
    const int frame = qBound(0, position, count - 1);
    return QRect((frame % columns) * frameSize.width(),
                 (frame / columns) * frameSize.height(),
                 frameSize.width(), frameSize.height());
}

QTransform imageLayerTransform(const LayerPtr& layer)
{
    QTransform transform;
    if (!layer) return transform;
    if (layer->type != LayerType::Image || layer->image.isNull()) {
        transform.translate(layer->offsetx, layer->offsety);
        return transform;
    }
    const QSize frameSize = visualLayerFrameSize(layer);
    const double sx=qBound(0.01,layer->imageScaleX,100.0), sy=qBound(0.01,layer->imageScaleY,100.0);
    const double drawW=frameSize.width()*sx, drawH=frameSize.height()*sy;
    transform.translate(layer->offsetx+drawW/2.0,layer->offsety+drawH/2.0);
    transform.rotate(layer->imageRotation);
    transform.scale(layer->imageFlipX?-sx:sx,layer->imageFlipY?-sy:sy);
    transform.translate(-frameSize.width()/2.0,-frameSize.height()/2.0);
    return transform;
}

TilesetPixmapCache& pixmapCache()
{
    static TilesetPixmapCache c;
    return c;
}

namespace {

constexpr int kMaskChunkTiles = 16;

struct MaskPathCacheState {
    QHash<QString, QHash<quint64, QPainterPath>> byLayer;
};

MaskPathCacheState& maskPathCacheState()
{
    static MaskPathCacheState cache;
    return cache;
}

quint64 maskChunkKey(int cx, int cy)
{
    return (quint64(quint32(cx)) << 32) | quint32(cy);
}

QRect visibleTileRange(const LayerPtr& layer, const QRectF& clip)
{
    if (!layer || layer->type != LayerType::Tile || layer->cols <= 0 || layer->rows <= 0)
        return QRect();
    const int tw = qMax(1, layer->tileWidth);
    const int th = qMax(1, layer->tileHeight);
    const QRectF layerRect(layer->offsetx, layer->offsety, layer->cols * tw, layer->rows * th);
    QRectF effective = clip;
    if (!effective.isValid() || effective.isNull()) effective = layerRect;
    else effective = effective.intersected(layerRect);
    if (!effective.isValid() || effective.isEmpty()) return QRect();
    const int x0 = qBound(0, int(std::floor((effective.left() - layer->offsetx) / tw)), layer->cols - 1);
    const int y0 = qBound(0, int(std::floor((effective.top() - layer->offsety) / th)), layer->rows - 1);
    const int x1 = qBound(0, int(std::ceil((effective.right() - layer->offsetx) / tw)), layer->cols - 1);
    const int y1 = qBound(0, int(std::ceil((effective.bottom() - layer->offsety) / th)), layer->rows - 1);
    if (x1 < x0 || y1 < y0) return QRect();
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}


void mixVisualHash(quint64& h, quint64 v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}

void hashContactSubtree(const Editor& ed, const QVector<LayerPtr>& nodes,
                        const QRectF& clip, quint64& h)
{
    for (const LayerPtr& layer : nodes) {
        if (!layer || !layer->visible) continue;
        mixVisualHash(h, quint64(qHash(layer->id)));
        mixVisualHash(h, quint64(qRound64(layer->opacity * 100000.0)));
        mixVisualHash(h, quint64(quint32(layer->offsetx)));
        mixVisualHash(h, quint64(quint32(layer->offsety)));
        mixVisualHash(h, layer->imageMaskEnabled ? 1ULL : 0ULL);
        auto hashFilter = [&](const RasterLayerFilter& f) {
            mixVisualHash(h, quint64(qHash(f.type)));
            mixVisualHash(h, f.enabled ? 1ULL : 0ULL);
            mixVisualHash(h, quint64(qRound64(f.radius * 100.0)));
            mixVisualHash(h, quint64(qRound64(f.strength * 1000.0)));
            mixVisualHash(h, quint64(qRound64(f.angle * 100.0)));
            mixVisualHash(h, quint64(qRound64(f.amount * 1000.0)));
            mixVisualHash(h, quint64(qRound64(f.scale * 100.0)));
            mixVisualHash(h, quint64(quint32(f.seed)));
        };
        for (const RasterLayerFilter& f : layer->maskFilters) hashFilter(f);
        if (layer->type == LayerType::Image)
            for (const RasterLayerFilter& f : layer->imageFilters)
                if (f.type != QLatin1String("contactShadow")) hashFilter(f);

        if (layer->type == LayerType::Tile) {
            const QRect range = visibleTileRange(layer, clip);
            if (range.isValid() && !range.isEmpty()) {
                for (int y = range.top(); y <= range.bottom(); ++y) {
                    if (y < 0 || y >= layer->data2D.size()) continue;
                    const QVector<Cell>& row = layer->data2D[y];
                    for (int x = range.left(); x <= range.right() && x < row.size(); ++x) {
                        if (x < 0) continue;
                        const Cell& cell = row[x];
                        mixVisualHash(h, quint64((x + 1) * 73856093u) ^ quint64((y + 1) * 19349663u));
                        for (const TileRef& tile : cell) {
                            // Tiles com prioridade não participam da silhueta da sombra.
                            if (ed.tilePriority(tile.tilesetIdx, tile.tx, tile.ty) > 0) continue;
                            mixVisualHash(h, quint64(tile.tilesetIdx + 1));
                            mixVisualHash(h, quint64(tile.tx + 1));
                            mixVisualHash(h, quint64(tile.ty + 1));
                            if (const Tileset* ts = ed.tilesetAt(tile.tilesetIdx))
                                mixVisualHash(h, quint64(ts->image.cacheKey()));
                        }
                    }
                }
            }
            if (layer->imageMaskEnabled && !layer->imageMask.isNull())
                mixVisualHash(h, quint64(layer->imageMask.cacheKey()));
        } else if (layer->type == LayerType::Image) {
            mixVisualHash(h, quint64(layer->image.cacheKey()));
            mixVisualHash(h, quint64(layer->imageMask.cacheKey()));
            mixVisualHash(h, quint64(qRound64(layer->imageScaleX * 10000.0)));
            mixVisualHash(h, quint64(qRound64(layer->imageScaleY * 10000.0)));
            mixVisualHash(h, quint64(qRound64(layer->imageRotation * 100.0)));
        } else if (layer->type == LayerType::Object) {
            mixVisualHash(h, quint64(layer->objects.size()));
            for (const MapObject& obj : layer->objects) {
                if (!obj.visible) continue;
                mixVisualHash(h, quint64(qHash(obj.id)));
                mixVisualHash(h, quint64(qRound64(obj.x * 100.0)));
                mixVisualHash(h, quint64(qRound64(obj.y * 100.0)));
                mixVisualHash(h, quint64(qRound64(obj.w * 100.0)));
                mixVisualHash(h, quint64(qRound64(obj.h * 100.0)));
                mixVisualHash(h, quint64(qRound64(obj.rotation * 100.0)));
                for (const TileRef& tile : obj.tiles) {
                    if (ed.tilePriority(tile.tilesetIdx, tile.tx, tile.ty) > 0) continue;
                    mixVisualHash(h, quint64(tile.tilesetIdx + 1));
                    mixVisualHash(h, quint64(tile.tx + 1));
                    mixVisualHash(h, quint64(tile.ty + 1));
                    if (const Tileset* ts = ed.tilesetAt(tile.tilesetIdx))
                        mixVisualHash(h, quint64(ts->image.cacheKey()));
                }
            }
        }
        if (!layer->children.isEmpty()) hashContactSubtree(ed, layer->children, clip, h);
    }
}

QPainterPath buildMaskChunkPath(const LayerPtr& layer, int chunkX, int chunkY)
{
    QPainterPath path;
    if (!layer) return path;
    const int x0 = chunkX * kMaskChunkTiles;
    const int y0 = chunkY * kMaskChunkTiles;
    const int x1 = qMin(layer->cols, x0 + kMaskChunkTiles);
    const int y1 = qMin(layer->rows, y0 + kMaskChunkTiles);
    for (int y = qMax(0, y0); y < y1; ++y) {
        if (y >= layer->data2D.size()) break;
        const QVector<Cell>& row = layer->data2D[y];
        for (int x = qMax(0, x0); x < x1 && x < row.size(); ++x) {
            if (row[x].isEmpty()) continue;
            path.addRect(QRectF(x * layer->tileWidth + layer->offsetx,
                                y * layer->tileHeight + layer->offsety,
                                layer->tileWidth, layer->tileHeight));
        }
    }
    return path;
}

QPainterPath maskPathForClip(const LayerPtr& layer, const QRectF& clip, bool useCache, RendererViewportStats* stats,
                             const std::function<Cell(const LayerPtr&,int,int)>& cellResolver)
{
    QPainterPath result;
    if (layer && layer->type == LayerType::Image && !layer->image.isNull()) {
        QPainterPath local;
        const QImage source=layer->image.convertToFormat(QImage::Format_ARGB32);
        QImage rasterMask;
        if(layer->imageMaskEnabled&&!layer->imageMask.isNull())rasterMask=layer->imageMask.scaled(source.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<source.height();++y){
            const QRgb* row=reinterpret_cast<const QRgb*>(source.constScanLine(y));
            const QRgb* maskRow=rasterMask.isNull()?nullptr:reinterpret_cast<const QRgb*>(rasterMask.constScanLine(y));
            int start=-1;
            for(int x=0;x<=source.width();++x){
                const int maskAlpha=maskRow&&x<source.width()?(qGray(maskRow[x])*qAlpha(maskRow[x])+127)/255:255;
                const bool opaque=x<source.width()&&qAlpha(row[x])*maskAlpha>0;
                if(opaque&&start<0)start=x;
                else if(!opaque&&start>=0){local.addRect(QRectF(start,y,x-start,1));start=-1;}
            }
        }
        result=imageLayerTransform(layer).map(local);
        if(clip.isValid()&&!clip.isNull()){QPainterPath clipPath;clipPath.addRect(clip);result=result.intersected(clipPath);}
        return result;
    }
    const QRect tiles = visibleTileRange(layer, clip);
    if (!tiles.isValid() || tiles.isEmpty()) return result;

    if (!useCache || cellResolver) {
        for (int y = tiles.top(); y <= tiles.bottom(); ++y) {
            if (y < 0 || y >= layer->data2D.size()) continue;
            const QVector<Cell>& row = layer->data2D[y];
            for (int x = tiles.left(); x <= tiles.right() && x < row.size(); ++x) {
                if (x < 0) continue;
                const Cell effective = cellResolver ? cellResolver(layer,x,y) : row[x];
                if (effective.isEmpty()) continue;
                result.addRect(QRectF(x * layer->tileWidth + layer->offsetx,
                                      y * layer->tileHeight + layer->offsety,
                                      layer->tileWidth, layer->tileHeight));
            }
        }
        return result;
    }

    auto& chunks = maskPathCacheState().byLayer[layer->id];
    const int cx0 = tiles.left() / kMaskChunkTiles;
    const int cy0 = tiles.top() / kMaskChunkTiles;
    const int cx1 = tiles.right() / kMaskChunkTiles;
    const int cy1 = tiles.bottom() / kMaskChunkTiles;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const quint64 key = maskChunkKey(cx, cy);
            auto it = chunks.constFind(key);
            if (it == chunks.constEnd()) {
                const QPainterPath built = buildMaskChunkPath(layer, cx, cy);
                chunks.insert(key, built);
                if (stats) ++stats->maskChunksBuilt;
                result.addPath(built);
            } else {
                if (stats) ++stats->maskChunksReused;
                result.addPath(it.value());
            }
        }
    }
    return result;
}

} // namespace

QPainterPath layerClipPath(const LayerPtr& layer,const QRectF& clip)
{
    return maskPathForClip(layer,clip,false,nullptr,{});
}

void invalidateMaskPathCache(const QString& layerId)
{
    auto& cache = maskPathCacheState().byLayer;
    if (layerId.isEmpty()) cache.clear();
    else cache.remove(layerId);
}

const QPixmap& TilesetPixmapCache::pixmap(const Editor& ed, int tilesetIdx)
{
    static const QPixmap empty;
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || ts->image.isNull()) return empty;
    const qint64 key = ts->image.cacheKey();
    if (!m_cache.contains(tilesetIdx) || m_keys.value(tilesetIdx) != key) {
        m_cache.insert(tilesetIdx, QPixmap::fromImage(ts->image));
        m_keys.insert(tilesetIdx, key);
    }
    return m_cache[tilesetIdx];
}

void TilesetPixmapCache::invalidate(int tilesetIdx)
{
    if (tilesetIdx < 0) { m_cache.clear(); m_keys.clear(); }
    else { m_cache.remove(tilesetIdx); m_keys.remove(tilesetIdx); }
}

QPainter::CompositionMode compositionFor(const QString& blendMode)
{
    if (blendMode == QLatin1String("add"))         return QPainter::CompositionMode_Plus;
    if (blendMode == QLatin1String("multiply"))    return QPainter::CompositionMode_Multiply;
    if (blendMode == QLatin1String("screen"))      return QPainter::CompositionMode_Screen;
    if (blendMode == QLatin1String("overlay"))     return QPainter::CompositionMode_Overlay;
    if (blendMode == QLatin1String("darken"))      return QPainter::CompositionMode_Darken;
    if (blendMode == QLatin1String("lighten"))     return QPainter::CompositionMode_Lighten;
    if (blendMode == QLatin1String("difference"))  return QPainter::CompositionMode_Difference;
    if (blendMode == QLatin1String("exclusion"))   return QPainter::CompositionMode_Exclusion;
    if (blendMode == QLatin1String("hard-light"))  return QPainter::CompositionMode_HardLight;
    if (blendMode == QLatin1String("soft-light"))  return QPainter::CompositionMode_SoftLight;
    if (blendMode == QLatin1String("color-dodge")) return QPainter::CompositionMode_ColorDodge;
    if (blendMode == QLatin1String("color-burn"))  return QPainter::CompositionMode_ColorBurn;
    return QPainter::CompositionMode_SourceOver;
}

void applyScreenTone(QImage& image, int red, int green, int blue, int gray)
{
    const int toneR = qBound(-255, red, 255);
    const int toneG = qBound(-255, green, 255);
    const int toneB = qBound(-255, blue, 255);
    const int toneGray = qBound(0, gray, 255);
    if (image.isNull() ||
        (toneR == 0 && toneG == 0 && toneB == 0 && toneGray == 0))
        return;

    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const int keep = 255 - toneGray;
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) continue;

            // O buffer e premultiplicado. Despremultiplica, altera a cor e
            // premultiplica de novo para manter bordas transparentes corretas.
            int r = a == 255 ? qRed(px)
                             : qBound(0, (qRed(px) * 255 + a / 2) / a, 255);
            int g = a == 255 ? qGreen(px)
                             : qBound(0, (qGreen(px) * 255 + a / 2) / a, 255);
            int b = a == 255 ? qBlue(px)
                             : qBound(0, (qBlue(px) * 255 + a / 2) / a, 255);

            r = qBound(0, r + toneR, 255);
            g = qBound(0, g + toneG, 255);
            b = qBound(0, b + toneB, 255);

            if (toneGray > 0) {
                // Luma BT.601. Gray=255 dessatura completamente, sem pintar
                // a tela de cinza por cima da cena.
                const int luma = (299 * r + 587 * g + 114 * b + 500) / 1000;
                r = (r * keep + luma * toneGray + 127) / 255;
                g = (g * keep + luma * toneGray + 127) / 255;
                b = (b * keep + luma * toneGray + 127) / 255;
            }

            if (a != 255) {
                r = (r * a + 127) / 255;
                g = (g * a + 127) / 255;
                b = (b * a + 127) / 255;
            }
            line[x] = qRgba(r, g, b, a);
        }
    }
}

static void drawObject(QPainter& p, const Editor& ed, const MapObject& o,
                       const LayerPtr& layer, const RenderOptions& opt)
{
    if (!o.visible) return;
    const double ox = o.x + layer->offsetx, oy = o.y + layer->offsety;
    const double normalized = std::fmod(o.rotation, 360.0);
    const int sw = qMax(1, o.stampW), sh = qMax(1, o.stampH);

    // Tamanho fonte real do stamp. A largura/altura do MapObject continuam
    // sendo o tamanho visual/final; isso permite expor escala em porcentagem
    // sem alterar o formato antigo do projeto.
    int nativeCellW = qMax(1, ed.mapInfo().tileWidth);
    int nativeCellH = qMax(1, ed.mapInfo().tileHeight);
    for (const TileRef& tile : o.tiles) {
        const Tileset* ts = ed.tilesetAt(tile.tilesetIdx);
        if (!ts || !ts->contains(tile.tx, tile.ty)) continue;
        nativeCellW = qMax(1, ts->tilewidth);
        nativeCellH = qMax(1, ts->tileheight);
        break;
    }
    const QSize nativeSize(qMax(1, sw * nativeCellW), qMax(1, sh * nativeCellH));
    const QSize finalSize(qMax(1, qRound(o.w)), qMax(1, qRound(o.h)));

    auto drawUnrotated = [&](QPainter& painter, double x, double y, double width, double height) {
        if (!o.tiles.isEmpty()) {
            const double cw = width / sw, ch = height / sh;
            for (int i = 0; i < o.tiles.size(); ++i) {
                const TileRef& t = o.tiles[i];
                if (opt.tileFilter && !opt.tileFilter(t)) continue;
                const Tileset* ts = ed.tilesetAt(t.tilesetIdx);
                if (!ts || !ts->contains(t.tx, t.ty)) continue;
                const int dx = i % sw, dy = i / sw;
                painter.drawPixmap(QRectF(x + dx * cw, y + dy * ch, cw, ch),
                                   pixmapCache().pixmap(ed, t.tilesetIdx),
                                   QRectF(animatedTileRect(*ts, t.tx, t.ty, opt.animationTimeMs,
                                                          quint32(qHash(o.id) ^ quint32(i)))));
            }
        } else if (!opt.tileFilter) {
            painter.fillRect(QRectF(x, y, width, height), QColor(74, 144, 215, 60));
        }
    };

    // Passo edge-aware usado tanto pelo xBR de escala quanto pelo RotSprite.
    // Mantem contornos diagonais mais limpos que uma ampliacao nearest pura.
    auto edgeScale2x = [](const QImage& input) {
        const QImage src = input.convertToFormat(QImage::Format_ARGB32);
        if (src.isNull()) return QImage();
        QImage dst(src.width() * 2, src.height() * 2, QImage::Format_ARGB32);
        if (dst.isNull()) return QImage();
        for (int y = 0; y < src.height(); ++y) {
            const QRgb* current = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            const QRgb* up = reinterpret_cast<const QRgb*>(src.constScanLine(qMax(0, y - 1)));
            const QRgb* down = reinterpret_cast<const QRgb*>(src.constScanLine(qMin(src.height() - 1, y + 1)));
            QRgb* row0 = reinterpret_cast<QRgb*>(dst.scanLine(y * 2));
            QRgb* row1 = reinterpret_cast<QRgb*>(dst.scanLine(y * 2 + 1));
            for (int x = 0; x < src.width(); ++x) {
                const QRgb P = current[x];
                const QRgb A = up[x];
                const QRgb B = current[qMin(src.width() - 1, x + 1)];
                const QRgb C = current[qMax(0, x - 1)];
                const QRgb D = down[x];
                row0[x * 2]     = (C == A && C != D && A != B) ? A : P;
                row0[x * 2 + 1] = (A == B && A != C && B != D) ? B : P;
                row1[x * 2]     = (D == C && D != B && C != A) ? C : P;
                row1[x * 2 + 1] = (B == D && B != A && D != C) ? D : P;
            }
        }
        return dst;
    };

    auto composeNativeImage = [&]() {
        const int width = nativeSize.width(), height = nativeSize.height();
        if (width > 8192 || height > 8192 || qint64(width) * height > 32LL * 1024LL * 1024LL)
            return QImage();
        QImage base(width, height, QImage::Format_ARGB32_Premultiplied);
        if (base.isNull()) return QImage();
        base.fill(Qt::transparent);
        QPainter bp(&base);
        bp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        drawUnrotated(bp, 0, 0, width, height);
        bp.end();
        return base;
    };

    auto scaleImage = [&](const QImage& base) {
        if (base.isNull()) return QImage();
        if (base.size() == finalSize) return base;
        if (o.scaleFilter == QLatin1String("smooth"))
            return base.scaled(finalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (o.scaleFilter != QLatin1String("xbr"))
            return base.scaled(finalSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);

        // xBR editorial: ampliacao edge-aware em potencias de 2 e ajuste final
        // nearest. Para reducao usamos smooth, pois xBR e um upscaler.
        if (finalSize.width() < base.width() || finalSize.height() < base.height())
            return base.scaled(finalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage enlarged = base;
        constexpr qint64 kMaxXbrPixels = 32LL * 1024LL * 1024LL;
        while ((enlarged.width() < finalSize.width() || enlarged.height() < finalSize.height()) &&
               qint64(enlarged.width()) * enlarged.height() * 4 <= kMaxXbrPixels) {
            QImage next = edgeScale2x(enlarged);
            if (next.isNull()) break;
            enlarged = next;
        }
        return enlarged.scaled(finalSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    };

    auto rotateImage = [&](const QImage& base) {
        if (base.isNull()) return QImage();
        const double angle = normalized;
        if (std::abs(angle) < 0.000001) return base;
        QTransform transform;
        transform.rotate(angle);
        const double quarterTurns = std::round(angle / 90.0);
        if (std::abs(angle - quarterTurns * 90.0) < 0.000001)
            return base.transformed(transform, Qt::FastTransformation);
        if (o.rotationFilter == QLatin1String("smooth"))
            return base.transformed(transform, Qt::SmoothTransformation);
        if (o.rotationFilter == QLatin1String("nearest"))
            return base.transformed(transform, Qt::FastTransformation);

        // RotSprite adaptativo: amplia edge-aware, gira por nearest e reduz.
        int levels = 3;
        constexpr qint64 kMaxUpscaledPixels = 16LL * 1024LL * 1024LL;
        while (levels > 0 &&
               qint64(base.width()) * (1LL << levels) * base.height() * (1LL << levels) > kMaxUpscaledPixels)
            --levels;
        if (levels == 0) return base.transformed(transform, Qt::FastTransformation);
        QImage enlarged = base;
        for (int i = 0; i < levels; ++i) {
            QImage next = edgeScale2x(enlarged);
            if (next.isNull()) break;
            enlarged = next;
        }
        QImage rotated = enlarged.transformed(transform, Qt::FastTransformation);
        if (rotated.isNull()) return QImage();
        const int divisor = 1 << levels;
        const QSize reduced(qMax(1, qRound(double(rotated.width()) / divisor)),
                            qMax(1, qRound(double(rotated.height()) / divisor)));
        return rotated.scaled(reduced, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    };

    const bool scaled = finalSize != nativeSize;
    const bool needsFilteredScale = scaled && o.scaleFilter != QLatin1String("nearest");
    const bool rotated = std::abs(normalized) >= 0.000001;

    // Caminho antigo/mais barato continua para nearest sem rotacao.
    if (!rotated && !needsFilteredScale) {
        drawUnrotated(p, ox, oy, o.w, o.h);
    } else {
        struct ObjectTransformCache {
            QCache<QString, QImage> images;
            ObjectTransformCache() : images(96 * 1024) {} // custo em KiB, ~96 MiB
        };
        static ObjectTransformCache cache;

        QString key;
        if (!opt.tileFilter) {
            key.reserve(192 + o.tiles.size() * 32);
            key += o.id + QLatin1Char('|') + o.scaleFilter + QLatin1Char('|')
                + o.rotationFilter + QLatin1Char('|')
                + QString::number(qRound(o.w * 1000.0)) + QLatin1Char('x')
                + QString::number(qRound(o.h * 1000.0)) + QLatin1Char('|')
                + QString::number(qRound(normalized * 1000.0));
            for (int i = 0; i < o.tiles.size(); ++i) {
                const TileRef& tile = o.tiles[i];
                const Tileset* ts = ed.tilesetAt(tile.tilesetIdx);
                if (!ts) continue;
                const QRect source = animatedTileRect(*ts, tile.tx, tile.ty, opt.animationTimeMs,
                                                      quint32(qHash(o.id) ^ quint32(i)));
                key += QLatin1Char('|') + QString::number(tile.tilesetIdx) + QLatin1Char(':')
                    + QString::number(tile.tx) + QLatin1Char(',') + QString::number(tile.ty) + QLatin1Char('@')
                    + QString::number(ts->image.cacheKey()) + QLatin1Char(':')
                    + QString::number(source.x()) + QLatin1Char(',') + QString::number(source.y());
            }
        }

        auto buildTransformed = [&]() {
            return rotateImage(scaleImage(composeNativeImage()));
        };
        QImage transformed;
        if (!key.isEmpty()) {
            if (QImage* cached = cache.images.object(key)) transformed = *cached;
            else {
                transformed = buildTransformed();
                if (!transformed.isNull()) {
                    const int cost = qMax(1, int(qMin<qint64>(std::numeric_limits<int>::max(),
                        qint64(transformed.sizeInBytes()) / 1024 + 1)));
                    cache.images.insert(key, new QImage(transformed), cost);
                }
            }
        } else {
            transformed = buildTransformed();
        }

        if (!transformed.isNull()) {
            const QRectF target = rotated
                ? paint::objectBounds(o).translated(layer->offsetx, layer->offsety)
                : QRectF(ox, oy, o.w, o.h);
            p.save();
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.drawImage(target, transformed);
            p.restore();
        } else {
            // Fallback seguro em baixa memoria: preserva geometria, mesmo sem
            // o filtro edge-aware.
            p.save();
            p.setRenderHint(QPainter::SmoothPixmapTransform,
                            o.scaleFilter == QLatin1String("smooth") ||
                            o.rotationFilter == QLatin1String("smooth"));
            const QPointF center(ox + o.w / 2.0, oy + o.h / 2.0);
            p.translate(center); p.rotate(o.rotation); p.translate(-center);
            drawUnrotated(p, ox, oy, o.w, o.h);
            p.restore();
        }
    }
    if (opt.drawObjectFrames) {
        QPen framePen(QColor(74, 144, 215, 200));
        framePen.setWidth(0);
        p.setPen(framePen);
        p.setBrush(Qt::NoBrush);
        MapObject visual = o;
        visual.x += layer->offsetx;
        visual.y += layer->offsety;
        p.drawPolygon(paint::objectPolygon(visual));
    }
}

static QImage composeLayerImageWithMask(const LayerPtr& layer, bool suppressContactShadows = false)
{
    if (!layer || layer->image.isNull()) return QImage();

    // Sombra de contato precisa enxergar a silhueta FINAL, inclusive a máscara.
    // Por isso ela é um pós-processo da camada; blur/ruído continuam antes da
    // máscara, preservando a separação conteúdo -> máscara -> contato.
    QVector<RasterLayerFilter> contentFilters, contactFilters;
    contentFilters.reserve(layer->imageFilters.size());
    for (const RasterLayerFilter& f : layer->imageFilters) {
        if (f.type == QLatin1String("contactShadow")) {
            if (!suppressContactShadows) contactFilters.push_back(f);
        } else contentFilters.push_back(f);
    }

    QImage content = filteredRasterCached(layer->image, contentFilters, false);
    if (layer->imageMaskEnabled && !layer->imageMask.isNull()) {
        const QImage mask = filteredRasterCached(layer->imageMask, layer->maskFilters, true);
        content = maskedRasterCached(content, mask);
    }
    return filteredRasterCached(content, contactFilters, false);
}

static QImage visualEffectPreviewCached(const QImage& source, const LayerPtr& layer)
{
    if (source.isNull() || !layer || layer->parallaxEffectPreset == QLatin1String("none") ||
        layer->parallaxEffectStrength <= .001) return source;
    static QCache<QString, QImage> cache(96 * 1024);
    const double strength = qBound(0.0, layer->parallaxEffectStrength, 1.0);
    const QString key = QStringLiteral("visual-fx|%1|%2|%3")
        .arg(source.cacheKey()).arg(layer->parallaxEffectPreset).arg(strength, 0, 'f', 3);
    if (QImage* hit = cache.object(key)) return *hit;

    QColor tint;
    double tintAmount = strength;
    double brightness = 1.0;
    if (layer->parallaxEffectPreset == QLatin1String("underwater")) { tint = QColor(55, 145, 190); tintAmount *= .58; brightness = .92; }
    else if (layer->parallaxEffectPreset == QLatin1String("mist")) { tint = QColor(205, 220, 225); tintAmount *= .48; brightness = 1.06; }
    else if (layer->parallaxEffectPreset == QLatin1String("dream")) { tint = QColor(166, 120, 205); tintAmount *= .38; brightness = 1.08; }
    else if (layer->parallaxEffectPreset == QLatin1String("heat")) { tint = QColor(235, 126, 64); tintAmount *= .34; brightness = 1.03; }
    else if (layer->parallaxEffectPreset == QLatin1String("ghost")) { tint = QColor(125, 225, 218); tintAmount *= .62; brightness = 1.05; }
    else if (layer->parallaxEffectPreset == QLatin1String("night")) { tint = QColor(42, 65, 128); tintAmount *= .70; brightness = .72; }
    else return source;

    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb px = row[x];
            const auto channel = [&](int base, int target) {
                return qBound(0, int(std::lround((base + (target - base) * tintAmount) * brightness)), 255);
            };
            row[x] = qRgba(channel(qRed(px), tint.red()), channel(qGreen(px), tint.green()),
                           channel(qBlue(px), tint.blue()), qAlpha(px));
        }
    }
    const int costKb = qMax(1, int(qMin<qint64>(out.sizeInBytes() / 1024, 32 * 1024)));
    cache.insert(key, new QImage(out), costKb);
    return out;
}

void drawLayer(QPainter& p, const Editor& ed, const LayerPtr& layer,
               double extraAlpha, const RenderOptions& opt)
{
    if (!layer || !layer->visible) return;
    const double alpha = layer->opacity * extraAlpha;
    if (alpha <= 0.001) return;

    p.save();
    p.setOpacity(alpha);
    p.setCompositionMode(compositionFor(layer->blendMode));

    if (layer->type == LayerType::Tile) {
        const int tw = layer->tileWidth, th = layer->tileHeight;
        auto drawTileCells = [&](QPainter& targetPainter) {
            // Recorta ao retangulo visivel para nao percorrer o mapa inteiro.
            const QRectF clip = targetPainter.clipBoundingRect();
            int x0 = 0, y0 = 0, x1 = layer->cols - 1, y1 = layer->rows - 1;
            if (!clip.isNull() && clip.isValid()) {
                x0 = qMax(0, int(std::floor((clip.left()   - layer->offsetx) / tw)) - 1);
                y0 = qMax(0, int(std::floor((clip.top()    - layer->offsety) / th)) - 1);
                x1 = qMin(layer->cols - 1, int(std::ceil((clip.right()  - layer->offsetx) / tw)) + 1);
                y1 = qMin(layer->rows - 1, int(std::ceil((clip.bottom() - layer->offsety) / th)) + 1);
            }
            for (int y = y0; y <= y1; ++y) {
                if (y < 0 || y >= layer->data2D.size()) continue;
                const QVector<Cell>& row = layer->data2D[y];
                for (int x = x0; x <= x1; ++x) {
                    if (x < 0 || x >= row.size()) continue;
                    if (opt.viewportStats) ++opt.viewportStats->tileCellsVisited;
                    if (opt.cellFilter && !opt.cellFilter(layer, x, y)) continue;
                    const Cell cell = opt.cellResolver ? opt.cellResolver(layer, x, y) : row[x];
                    for (const TileRef& c : cell) {
                        if (opt.tileFilter && !opt.tileFilter(c)) continue;
                        const Tileset* ts = ed.tilesetAt(c.tilesetIdx);
                        if (!ts || !ts->contains(c.tx, c.ty)) continue;
                        targetPainter.drawPixmap(QRectF(x * tw + layer->offsetx, y * th + layer->offsety, tw, th),
                                                 pixmapCache().pixmap(ed, c.tilesetIdx),
                                                 QRectF(animatedTileRect(*ts, c.tx, c.ty, opt.animationTimeMs,
                                                                        quint32((x * 73856093) ^ (y * 19349663)))));
                    }
                }
            }
        };

        // Sombra de contato em Tile Layer -----------------------------------
        // O conteúdo continua sendo desenhado como tiles normais (inclusive
        // Autotiles animados). Somente a silhueta usada pela sombra é
        // rasterizada e cacheada, evitando transformar a Tile Layer numa imagem.
        QVector<RasterLayerFilter> tileContactFilters;
        for (const RasterLayerFilter& filter : layer->imageFilters)
            if (filter.enabled && filter.type == QLatin1String("contactShadow"))
                tileContactFilters.push_back(filter);

        if (!opt.suppressContactShadows && !tileContactFilters.isEmpty()) {
            double maxReach = 4.0;
            for (const RasterLayerFilter& filter : tileContactFilters)
                maxReach = qMax(maxReach, filter.radius * 3.0 + filter.spread + qAbs(filter.distance) + 4.0);
            const int margin = qBound(4, int(std::ceil(maxReach)), 256);
            const QRectF layerBounds(layer->offsetx, layer->offsety,
                                     qMax(1, layer->cols * tw), qMax(1, layer->rows * th));
            QRectF viewport = p.clipBoundingRect();
            if (!viewport.isValid() || viewport.isNull()) viewport = layerBounds;
            // Mantemos uma borda transparente ao redor da camada para que o
            // blur/offset não seja cortado na borda do mapa de tiles.
            const QRectF allowed = layerBounds.adjusted(-margin, -margin, margin, margin);
            const QRect shadowRect = viewport.adjusted(-margin, -margin, margin, margin)
                                               .intersected(allowed).toAlignedRect();
            if (!shadowRect.isEmpty() && shadowRect.width() <= 12288 && shadowRect.height() <= 12288) {
                const QImage filteredMask = (layer->imageMaskEnabled && !layer->imageMask.isNull())
                    ? filteredRasterCached(layer->imageMask, layer->maskFilters, true) : QImage();

                auto mixHash = [](quint64& h, quint64 v) {
                    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                };
                quint64 visualHash = 0xcbf29ce484222325ULL;
                const int x0 = qMax(0, int(std::floor((shadowRect.left() - layer->offsetx) / tw)) - 1);
                const int y0 = qMax(0, int(std::floor((shadowRect.top() - layer->offsety) / th)) - 1);
                const int x1 = qMin(layer->cols - 1, int(std::ceil((shadowRect.right() - layer->offsetx) / tw)) + 1);
                const int y1 = qMin(layer->rows - 1, int(std::ceil((shadowRect.bottom() - layer->offsety) / th)) + 1);
                for (int y = y0; y <= y1; ++y) {
                    if (y < 0 || y >= layer->data2D.size()) continue;
                    const QVector<Cell>& row = layer->data2D[y];
                    for (int x = x0; x <= x1; ++x) {
                        if (x < 0 || x >= row.size()) continue;
                        const Cell cell = opt.cellResolver ? opt.cellResolver(layer, x, y) : row[x];
                        mixHash(visualHash, quint64(x + 1)); mixHash(visualHash, quint64(y + 1));
                        mixHash(visualHash, quint64(cell.size()));
                        for (const TileRef& c : cell) {
                            mixHash(visualHash, quint64(c.tilesetIdx + 1));
                            mixHash(visualHash, quint64(c.tx + 1));
                            mixHash(visualHash, quint64(c.ty + 1));
                            const int priority = ed.tilePriority(c.tilesetIdx, c.tx, c.ty);
                            mixHash(visualHash, quint64(priority));
                            if (const Tileset* ts = ed.tilesetAt(c.tilesetIdx))
                                mixHash(visualHash, quint64(ts->image.cacheKey()));
                        }
                    }
                }
                mixHash(visualHash, filteredMask.isNull() ? 0ULL : quint64(filteredMask.cacheKey()));

                QString cacheKey = QStringLiteral("%1|%2,%3,%4,%5|%6")
                    .arg(layer->id).arg(shadowRect.x()).arg(shadowRect.y())
                    .arg(shadowRect.width()).arg(shadowRect.height()).arg(visualHash);
                for (const RasterLayerFilter& f : tileContactFilters) {
                    cacheKey += QStringLiteral("|%1:%2:%3:%4:%5:%6:%7:%8:%9")
                        .arg(f.id).arg(f.radius, 0, 'f', 2).arg(f.distance, 0, 'f', 2)
                        .arg(f.angle, 0, 'f', 2).arg(f.spread, 0, 'f', 2)
                        .arg(f.opacity, 0, 'f', 3).arg(f.strength, 0, 'f', 3)
                        .arg(f.quality).arg(f.color.rgba());
                }

                static QCache<QString, QImage> tileContactCache(160 * 1024);
                const bool cacheSafe = !opt.cellFilter && !opt.tileFilter && !opt.cellResolver;
                QImage shadowOverlay;
                if (cacheSafe) {
                    if (QImage* cached = tileContactCache.object(cacheKey)) shadowOverlay = *cached;
                }
                if (shadowOverlay.isNull()) {
                    QImage silhouette(shadowRect.size(), QImage::Format_ARGB32_Premultiplied);
                    silhouette.fill(Qt::transparent);
                    QPainter sp(&silhouette);
                    sp.setRenderHint(QPainter::SmoothPixmapTransform, false);
                    for (int y = y0; y <= y1; ++y) {
                        if (y < 0 || y >= layer->data2D.size()) continue;
                        const QVector<Cell>& row = layer->data2D[y];
                        for (int x = x0; x <= x1; ++x) {
                            if (x < 0 || x >= row.size()) continue;
                            if (opt.cellFilter && !opt.cellFilter(layer, x, y)) continue;
                            const Cell cell = opt.cellResolver ? opt.cellResolver(layer, x, y) : row[x];
                            for (const TileRef& c : cell) {
                                if (opt.tileFilter && !opt.tileFilter(c)) continue;
                                // Tiles com prioridade já criam profundidade por ordenação Y.
                                // Não entram como emissores de AO/sombra de contato.
                                if (ed.tilePriority(c.tilesetIdx, c.tx, c.ty) > 0) continue;
                                const Tileset* ts = ed.tilesetAt(c.tilesetIdx);
                                if (!ts || !ts->contains(c.tx, c.ty)) continue;
                                const QRectF target(x * tw + layer->offsetx - shadowRect.x(),
                                                    y * th + layer->offsety - shadowRect.y(), tw, th);
                                // Frame 0: a sombra não pisca/recalcula junto de
                                // Autotiles animados; só a silhueta estática é usada.
                                sp.drawPixmap(target, pixmapCache().pixmap(ed, c.tilesetIdx),
                                              QRectF(animatedTileRect(*ts, c.tx, c.ty, 0,
                                                                     quint32((x * 73856093) ^ (y * 19349663)))));
                            }
                        }
                    }
                    sp.end();

                    if (!filteredMask.isNull()) {
                        QImage rgba = silhouette.convertToFormat(QImage::Format_ARGB32);
                        for (int y = 0; y < rgba.height(); ++y) {
                            QRgb* dst = reinterpret_cast<QRgb*>(rgba.scanLine(y));
                            const int localY = shadowRect.y() + y - layer->offsety;
                            for (int x = 0; x < rgba.width(); ++x) {
                                const int localX = shadowRect.x() + x - layer->offsetx;
                                if (localX < 0 || localY < 0 || localX >= filteredMask.width() || localY >= filteredMask.height()) {
                                    dst[x] = 0; continue;
                                }
                                const QRgb m = filteredMask.pixel(localX, localY);
                                const int mv = (qGray(m) * qAlpha(m) + 127) / 255;
                                const QRgb px = dst[x];
                                dst[x] = qRgba(qRed(px), qGreen(px), qBlue(px),
                                               (qAlpha(px) * mv + 127) / 255);
                            }
                        }
                        silhouette = rgba.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    }

                    shadowOverlay = QImage(silhouette.size(), QImage::Format_ARGB32_Premultiplied);
                    shadowOverlay.fill(Qt::transparent);
                    QPainter op(&shadowOverlay);
                    for (const RasterLayerFilter& filter : tileContactFilters) {
                        const QImage oneShadow = contactShadowOverlayCached(silhouette, filter);
                        if (!oneShadow.isNull()) op.drawImage(QPoint(0, 0), oneShadow);
                    }
                    op.end();
                    if (cacheSafe) {
                        const int costKb = qMax(1, int(qMin<qint64>(shadowOverlay.sizeInBytes() / 1024, 64 * 1024)));
                        tileContactCache.insert(cacheKey, new QImage(shadowOverlay), costKb);
                    }
                }
                if (!shadowOverlay.isNull()) p.drawImage(shadowRect.topLeft(), shadowOverlay);
            }
        }

        if (layer->imageMaskEnabled && !layer->imageMask.isNull()) {
            const QRectF layerBounds(layer->offsetx, layer->offsety,
                                     qMax(1, layer->cols * tw), qMax(1, layer->rows * th));
            QRectF visible = p.clipBoundingRect();
            if (!visible.isValid() || visible.isNull()) visible = layerBounds;
            visible = visible.intersected(layerBounds);
            const QRect clipRect = visible.toAlignedRect();
            if (!clipRect.isEmpty()) {
                QImage raster(clipRect.size(), QImage::Format_ARGB32_Premultiplied);
                raster.fill(Qt::transparent);
                QPainter rp(&raster);
                rp.setRenderHint(QPainter::SmoothPixmapTransform, false);
                rp.translate(-clipRect.x(), -clipRect.y());
                rp.setClipRect(clipRect);
                drawTileCells(rp);
                rp.end();

                const QImage mask = filteredRasterCached(layer->imageMask, layer->maskFilters, true);
                for (int y = 0; y < raster.height(); ++y) {
                    QRgb* dst = reinterpret_cast<QRgb*>(raster.scanLine(y));
                    const int localY = clipRect.y() + y - layer->offsety;
                    for (int x = 0; x < raster.width(); ++x) {
                        const int localX = clipRect.x() + x - layer->offsetx;
                        int mv = 255;
                        if (localX >= 0 && localY >= 0 && localX < mask.width() && localY < mask.height()) {
                            const QRgb m = mask.pixel(localX, localY);
                            mv = (qGray(m) * qAlpha(m) + 127) / 255;
                        }
                        if (mv >= 255) continue;
                        if (mv <= 0) { dst[x] = 0; continue; }
                        const QRgb px = dst[x];
                        dst[x] = qRgba((qRed(px) * mv + 127) / 255,
                                      (qGreen(px) * mv + 127) / 255,
                                      (qBlue(px) * mv + 127) / 255,
                                      (qAlpha(px) * mv + 127) / 255);
                    }
                }
                p.drawImage(clipRect.topLeft(), raster);
            }
        } else {
            drawTileCells(p);
        }
    } else if (layer->type == LayerType::Object) {
        const QRectF clip = p.clipBoundingRect();
        const bool preview = opt.previewLayerId == layer->id;
        const int insertion = qBound(0,opt.previewIndex,int(layer->objects.size()));
        for (int i=0; i<=layer->objects.size(); ++i) {
            if (preview && i==insertion) { p.save();p.setOpacity(p.opacity()*0.55);drawObject(p,ed,opt.previewObject,layer,opt);p.restore(); }
            if (i==layer->objects.size()) break;
            const MapObject& o=layer->objects[i];
            if (opt.viewportStats) ++opt.viewportStats->objectCandidates;
            if (clip.isValid() && !clip.isNull()) {
                const QRectF bounds = paint::objectBounds(o).translated(layer->offsetx, layer->offsety);
                if (!clip.intersects(bounds)) {
                    if (opt.viewportStats) ++opt.viewportStats->objectsCulled;
                    continue;
                }
            }
            drawObject(p, ed, o, layer, opt);
        }
    } else if (layer->type == LayerType::Image) {
        if ((!opt.skipReferenceLayers || !layer->imageReferenceOnly) && !layer->image.isNull()) {
            // Image Layer real: posicao vem de offsetx/offsety, enquanto escala,
            // flip, rotacao e filtro ficam salvos na propria camada. A rotacao
            // usa o centro da imagem escalada para produzir um comportamento
            // previsivel tanto no editor quanto na rasterizacao de exportacao.
            p.setOpacity(clampd(extraAlpha, 0.0, 1.0) * layer->opacity);
            const qint64 visualTime = opt.visualAnimationTimeMs >= 0
                ? opt.visualAnimationTimeMs : opt.animationTimeMs;
            const QRect frameRect = visualLayerFrameSourceRect(layer, visualTime);
            const QSize frameSize = frameRect.size();
            const double sx = qBound(0.01, layer->imageScaleX, 100.0);
            const double sy = qBound(0.01, layer->imageScaleY, 100.0);
            const double drawW = frameSize.width() * sx;
            const double drawH = frameSize.height() * sy;
            p.setRenderHint(QPainter::SmoothPixmapTransform,
                            layer->imageFilter == QLatin1String("bilinear"));
            QImage renderedImage = composeLayerImageWithMask(layer, opt.suppressContactShadows);
            if (opt.previewVisualEffects) renderedImage = visualEffectPreviewCached(renderedImage, layer);
            if (layer->parallaxAnimationEnabled && frameRect.isValid()) renderedImage = renderedImage.copy(frameRect);

            // Repetição da Image Layer é apenas uma forma de exibição: a
            // imagem-fonte continua única. Calculamos qual faixa local cobre
            // o clip atual para desenhar somente as cópias necessárias. Isso
            // mantém mapas grandes leves e também funciona com escala/rotação.
            const QRectF clipMap = p.clipBoundingRect();
            QTransform localToMap;
            localToMap.translate(layer->offsetx + drawW / 2.0, layer->offsety + drawH / 2.0);
            localToMap.rotate(layer->imageRotation);
            localToMap.scale(layer->imageFlipX ? -sx : sx, layer->imageFlipY ? -sy : sy);
            localToMap.translate(-frameSize.width() / 2.0, -frameSize.height() / 2.0);
            bool invertible = false;
            const QTransform mapToLocal = localToMap.inverted(&invertible);
            QRectF localVisible = invertible && clipMap.isValid() && !clipMap.isNull()
                ? mapToLocal.mapRect(clipMap)
                : QRectF(0, 0, frameSize.width(), frameSize.height());

            p.translate(layer->offsetx + drawW / 2.0, layer->offsety + drawH / 2.0);
            p.rotate(layer->imageRotation);
            p.scale(layer->imageFlipX ? -sx : sx, layer->imageFlipY ? -sy : sy);
            p.translate(-frameSize.width() / 2.0, -frameSize.height() / 2.0);

            const int iw = qMax(1, renderedImage.width());
            const int ih = qMax(1, renderedImage.height());
            const int x0 = layer->imageRepeatX ? int(std::floor(localVisible.left() / iw)) - 1 : 0;
            const int x1 = layer->imageRepeatX ? int(std::ceil(localVisible.right() / iw)) + 1 : 0;
            const int y0 = layer->imageRepeatY ? int(std::floor(localVisible.top() / ih)) - 1 : 0;
            const int y1 = layer->imageRepeatY ? int(std::ceil(localVisible.bottom() / ih)) + 1 : 0;
            for (int ry = y0; ry <= y1; ++ry)
                for (int rx = x0; rx <= x1; ++rx)
                    p.drawImage(QPointF(rx * iw, ry * ih), renderedImage);
        }
    }
    p.restore();
}

void drawMapPanorama(QPainter& p, const MapInfo& info)
{
    if (!info.panoramaVisible || info.panorama.isNull() || info.panoramaOpacity <= 0) return;
    const QRectF mapRect(0, 0, qMax(1, info.pixelWidth()), qMax(1, info.pixelHeight()));
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(qBound(0, info.panoramaOpacity, 255) / 255.0);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (info.panoramaFit) {
        p.drawImage(mapRect, info.panorama, QRectF(info.panorama.rect()));
    } else if (info.panoramaRepeat) {
        const int iw = qMax(1, info.panorama.width());
        const int ih = qMax(1, info.panorama.height());
        for (int y = 0; y < info.pixelHeight(); y += ih)
            for (int x = 0; x < info.pixelWidth(); x += iw)
                p.drawImage(QPointF(x, y), info.panorama);
    } else {
        p.drawImage(QPointF(0, 0), info.panorama);
    }
    p.restore();
}

void drawLayerTree(QPainter& p, const Editor& ed, const QVector<LayerPtr>& nodes,
                   double parentAlpha, const RenderOptions& opt)
{
    for (const LayerPtr& n : nodes) {
        if (!n || !n->visible) continue;
        // O filtro legado vale para folhas; grupos continuam sendo percorridos.
        if (opt.layerFilter && !n->isContainer() && !opt.layerFilter(n)) continue;
        // O filtro editorial decide se o conteudo visual deste no deve ser
        // desenhado. Containers ainda sao atravessados para preservar a ordem,
        // opacidade de grupos e clipping de mascaras.
        const bool drawThisNode = !opt.drawableFilter || opt.drawableFilter(n);

        RenderOptions childrenOptions = opt;
        if (n->id == opt.activeLayerId) childrenOptions.highlightActive = false;
        double alpha = parentAlpha;
        if (opt.highlightActive && !opt.activeLayerId.isEmpty() &&
            n->type != LayerType::Group && !layerSubtreeContains(n, opt.activeLayerId))
            alpha *= opt.focusDim;

        p.save();
        if (opt.previewParallax && n->parallaxLayer) {
            const qint64 visualTime = opt.visualAnimationTimeMs >= 0
                ? opt.visualAnimationTimeMs : opt.animationTimeMs;
            const double seconds = qMax<qint64>(0, visualTime) / 1000.0;
            const double phase = seconds * qBound(0.0, n->parallaxOscillationSpeed, 20.0) * 6.283185307179586;
            p.translate(opt.cameraOffset.x() * (1.0 - n->parallaxFactorX) + n->parallaxSpeedX * seconds + std::sin(phase) * n->parallaxOscillationX,
                        opt.cameraOffset.y() * (1.0 - n->parallaxFactorY) + n->parallaxSpeedY * seconds + std::cos(phase) * n->parallaxOscillationY);
            // Quando uma pasta inteira é uma Camada Visual, seus filhos seguem
            // o mesmo controlador e não recebem movimento uma segunda vez.
            childrenOptions.previewParallax = false;
        }

        if (n->isMask) {
            // Mascara: base + filhos recortados pela area preenchida da base.
            // Mesmo quando a base pertence a outro passe de foco, os filhos
            // continuam usando o path da mascara original para o clipping.
            if (n->maskShowBase && drawThisNode) drawLayer(p, ed, n, alpha, opt);
            if (!n->children.isEmpty() && (n->type == LayerType::Tile || n->type == LayerType::Image)) {
                // O path da mascara e construído somente para a area realmente
                // pintada. No editor, chunks de 16x16 ficam em cache entre repaints.
                const QPainterPath clipPath = maskPathForClip(n, p.clipBoundingRect(), opt.cacheMaskPaths, opt.viewportStats, opt.cellResolver);
                p.save();
                if (!clipPath.isEmpty()) p.setClipPath(clipPath, Qt::IntersectClip);
                else p.setClipRect(QRectF());   // mascara vazia esconde os filhos
                drawLayerTree(p, ed, n->children, alpha * n->opacity, childrenOptions);
                p.restore();
            }
        } else if (n->type == LayerType::Group) {
            QVector<RasterLayerFilter> groupContactFilters;
            if (!opt.suppressContactShadows) {
                for (const RasterLayerFilter& filter : n->imageFilters)
                    if (filter.enabled && filter.type == QLatin1String("contactShadow"))
                        groupContactFilters.push_back(filter);
            }

            if (!groupContactFilters.isEmpty() && !n->children.isEmpty()) {
                double maxReach = 4.0;
                for (const RasterLayerFilter& filter : groupContactFilters)
                    maxReach = qMax(maxReach, filter.radius * 3.0 + filter.spread + qAbs(filter.distance) + 4.0);
                const int margin = qBound(4, int(std::ceil(maxReach)), 256);
                const QRectF mapBounds(0, 0, qMax(1, ed.mapInfo().pixelWidth()), qMax(1, ed.mapInfo().pixelHeight()));
                QRectF viewport = p.clipBoundingRect();
                if (!viewport.isValid() || viewport.isNull()) viewport = mapBounds;
                const QRect shadowRect = viewport.adjusted(-margin, -margin, margin, margin)
                                                   .intersected(mapBounds.adjusted(-margin, -margin, margin, margin))
                                                   .toAlignedRect();
                if (!shadowRect.isEmpty() && shadowRect.width() <= 12288 && shadowRect.height() <= 12288) {
                    quint64 visualHash = 0xcbf29ce484222325ULL;
                    hashContactSubtree(ed, n->children, shadowRect, visualHash);
                    QString cacheKey = QStringLiteral("group-contact|%1|%2,%3,%4,%5|%6")
                        .arg(n->id).arg(shadowRect.x()).arg(shadowRect.y())
                        .arg(shadowRect.width()).arg(shadowRect.height()).arg(visualHash);
                    for (const RasterLayerFilter& f : groupContactFilters) {
                        cacheKey += QStringLiteral("|%1:%2:%3:%4:%5:%6:%7:%8:%9")
                            .arg(f.id).arg(f.radius, 0, 'f', 2).arg(f.distance, 0, 'f', 2)
                            .arg(f.angle, 0, 'f', 2).arg(f.spread, 0, 'f', 2)
                            .arg(f.opacity, 0, 'f', 3).arg(f.strength, 0, 'f', 3)
                            .arg(f.quality).arg(f.color.rgba());
                    }

                    static QCache<QString, QImage> groupContactCache(160 * 1024);
                    QImage shadowOverlay;
                    if (QImage* cached = groupContactCache.object(cacheKey)) shadowOverlay = *cached;
                    if (shadowOverlay.isNull()) {
                        QImage silhouette(shadowRect.size(), QImage::Format_ARGB32_Premultiplied);
                        silhouette.fill(Qt::transparent);
                        QPainter gp(&silhouette);
                        gp.setRenderHint(QPainter::SmoothPixmapTransform, false);
                        gp.translate(-shadowRect.topLeft());
                        gp.setClipRect(shadowRect);
                        RenderOptions silhouetteOptions = childrenOptions;
                        silhouetteOptions.drawObjectFrames = false;
                        silhouetteOptions.highlightActive = false;
                        silhouetteOptions.suppressContactShadows = true;
                        const auto inheritedTileFilter = silhouetteOptions.tileFilter;
                        silhouetteOptions.tileFilter = [&ed, inheritedTileFilter](const TileRef& tile) {
                            if (inheritedTileFilter && !inheritedTileFilter(tile)) return false;
                            return ed.tilePriority(tile.tilesetIdx, tile.tx, tile.ty) <= 0;
                        };
                        drawLayerTree(gp, ed, n->children, 1.0, silhouetteOptions);
                        gp.end();

                        shadowOverlay = QImage(silhouette.size(), QImage::Format_ARGB32_Premultiplied);
                        shadowOverlay.fill(Qt::transparent);
                        QPainter op(&shadowOverlay);
                        for (const RasterLayerFilter& filter : groupContactFilters) {
                            const QImage oneShadow = contactShadowOverlayCached(silhouette, filter);
                            if (!oneShadow.isNull()) op.drawImage(QPoint(0, 0), oneShadow);
                        }
                        op.end();
                        const int costKb = qMax(1, int(qMin<qint64>(shadowOverlay.sizeInBytes() / 1024, 64 * 1024)));
                        groupContactCache.insert(cacheKey, new QImage(shadowOverlay), costKb);
                    }
                    if (!shadowOverlay.isNull()) {
                        p.save();
                        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                        p.setOpacity(qBound(0.0, alpha * n->opacity, 1.0));
                        p.drawImage(shadowRect.topLeft(), shadowOverlay);
                        p.restore();
                    }
                }
            }
            drawLayerTree(p, ed, n->children, alpha * n->opacity, childrenOptions);
        } else {
            if (drawThisNode) drawLayer(p, ed, n, alpha, opt);
        }
        p.restore();
    }
}


QImage renderMapToImage(const Editor& ed, const MapDoc& doc,
                        const RenderOptions& opt, const QSize& target)
{
    const MapInfo& info = doc.map;
    const int mw = qMax(1, info.pixelWidth()), mh = qMax(1, info.pixelHeight());
    QImage img(mw, mh, QImage::Format_ARGB32_Premultiplied);
    img.fill(opt.fillBackground ? info.background : QColor(Qt::transparent));
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.setClipRect(0, 0, mw, mh);
        drawMapPanorama(p, info);
        drawLayerTree(p, ed, doc.layers, 1.0, opt);
    }
    if (target.isValid() && !target.isEmpty() && target != img.size())
        return img.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation);
    return img;
}

QImage renderMapToImage(const Editor& ed, const RenderOptions& opt, const QSize& target)
{
    const MapDoc* doc = ed.doc();
    if (!doc) return QImage();
    return renderMapToImage(ed, *doc, opt, target);
}

void drawGrid(QPainter& p, const Editor& ed, const QRectF& r, double zoom)
{
    if (!ed.session.showGrid) return;
    const MapInfo& info = ed.mapInfo();
    const int mw = info.pixelWidth(), mh = info.pixelHeight();

    // Grade é apenas overlay de linhas. O QPainter pode chegar aqui com brush,
    // opacidade ou composição deixados por eventos/overlays; sem isolar o
    // estado, o drawRect do contorno podia preencher o mapa inteiro de escuro.
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(1.0);
    p.setBrush(Qt::NoBrush);

    auto gridPass = [&](int tw, int th, int ox, int oy, const QColor& color) {
        if (tw <= 0 || th <= 0) return;
        if (tw * zoom < 4 || th * zoom < 4) return;   // grade densa demais: nao desenha
        p.setPen(QPen(color, 0));

        // A multigrade deve acompanhar o mesmo sistema de coordenadas da
        // camada ativa. Antes ela sempre nascia em (0,0), enquanto os tiles
        // podiam estar deslocados por offset X/Y, dando a impressao de pintura
        // fora da celula.
        const int firstX = int(std::floor((r.left() - ox) / double(tw))) * tw + ox;
        const int firstY = int(std::floor((r.top()  - oy) / double(th))) * th + oy;
        const int limitX = qMin(mw, int(std::ceil(r.right())) + tw);
        const int limitY = qMin(mh, int(std::ceil(r.bottom())) + th);
        const int yTop = qMax(0, int(std::floor(r.top())));
        const int yBottom = qMin(mh, int(std::ceil(r.bottom())));
        const int xLeft = qMax(0, int(std::floor(r.left())));
        const int xRight = qMin(mw, int(std::ceil(r.right())));
        for (int x = firstX; x <= limitX; x += tw)
            if (x >= 0 && x <= mw) p.drawLine(QPointF(x, yTop), QPointF(x, yBottom));
        for (int y = firstY; y <= limitY; y += th)
            if (y >= 0 && y <= mh) p.drawLine(QPointF(xLeft, y), QPointF(xRight, y));
    };

    gridPass(info.tileWidth, info.tileHeight, 0, 0, ed.session.gridColor);

    if (ed.session.multigrid) {
        const LayerPtr l = ed.activeLayer();
        if (l && l->type == LayerType::Tile &&
            (l->tileWidth != info.tileWidth || l->tileHeight != info.tileHeight ||
             l->offsetx != 0 || l->offsety != 0))
            gridPass(l->tileWidth, l->tileHeight, l->offsetx, l->offsety, QColor(74, 144, 215, 70));
    }

    // Contorno do mapa: nunca preencher, somente linha.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 160), 0));
    p.drawRect(QRectF(0, 0, mw, mh));
    p.restore();
}

} // namespace core
