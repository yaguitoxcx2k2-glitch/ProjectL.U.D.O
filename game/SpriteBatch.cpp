#include "SpriteBatch.h"
#include "StarActorDepth.h"
#include "core/TilesetOps.h"

#include <QColor>
#include <cmath>
#include <cstddef>

using namespace core;

namespace game {

namespace TextureKey {
QString tileset(int idx)
{
    // O2: esta chave é pedida dentro do loop de tiles. Cache por thread evita
    // formatar/alocar "ts:N" milhares de vezes durante um rebuild de chunk.
    thread_local QHash<int, QString> cache;
    const auto it = cache.constFind(idx);
    if (it != cache.constEnd()) return it.value();
    const QString key = QStringLiteral("ts:%1").arg(idx);
    cache.insert(idx, key);
    return key;
}
QString image(const QString& id) { return QStringLiteral("img:%1").arg(id); }
} // namespace TextureKey

void SpriteBatcher::clear()
{
    m_batches.clear();
}

int SpriteBatcher::totalQuads() const
{
    int n = 0;
    for (const DrawBatch& b : m_batches) n += b.quads();
    return n;
}

void SpriteBatcher::append(const SpriteBatcher& other)
{
    // O2: append de cache de chunks é um caminho quente. Reservar os slots de
    // batch e o merge de vértices evita crescimento em degraus quando a câmera
    // cruza regiões com muitos lotes.
    m_batches.reserve(m_batches.size() + other.m_batches.size());
    for (const DrawBatch& b : other.m_batches) {
        if (!m_batches.isEmpty() &&
            m_batches.last().texture == b.texture &&
            m_batches.last().blend == b.blend &&
            m_batches.last().affectedByScreenTone == b.affectedByScreenTone &&
            m_batches.last().smooth == b.smooth &&
            m_batches.last().space == b.space && m_batches.last().depth == b.depth) {
            QVector<QuadVertex>& verts = m_batches.last().verts;
            verts.reserve(verts.size() + b.verts.size());
            verts += b.verts;
        } else {
            m_batches.push_back(b);
        }
    }
}

void SpriteBatcher::add(const QString& texture, const QRectF& dst, const QRectF& src,
                        const QSize& texSize, double opacity, const QColor& tint,
                        PictureBlend blend, bool affectedByScreenTone,
                        RuntimeCoordinateSpace space, double depth)
{
    if (dst.isEmpty() || texSize.isEmpty()) return;
    // UV NAS BORDAS EXATAS DO RECORTE.
    //
    // O sampler da cena QRhi é Nearest. Para pixel art e zoom inteiro, o
    // intervalo precisa representar as BORDAS dos texels, não o centro do
    // primeiro ao centro do último. O antigo recuo de 0,5 texel fazia um tile
    // 32 px ampliado a 2x percorrer só 31 texels de distância; na prática um
    // dos dois pixels físicos já caía no texel seguinte e GPU/CPU divergiam.
    // Com nearest não existe vazamento do tile vizinho na fronteira, então as
    // bordas exatas também são a opção correta para atlas.
    const float u0 = float(src.left()   / texSize.width());
    const float v0 = float(src.top()    / texSize.height());
    const float u1 = float(src.right()  / texSize.width());
    const float v1 = float(src.bottom() / texSize.height());
    const QColor c = tint.isValid() ? tint : QColor(255, 255, 255);
    const float a = float(qBound(0.0, opacity, 1.0));
    const float mixAlpha = blend == PictureBlend::Normal ? 1.0f : a;
    const float r = float(c.redF())*mixAlpha, g = float(c.greenF())*mixAlpha,
                b = float(c.blueF())*mixAlpha;

    // Só emenda no lote anterior se for a MESMA textura: assim a ordem de
    // desenho (quem cobre quem) nunca é alterada pela otimização.
    if (m_batches.isEmpty() || m_batches.last().texture != texture ||
        m_batches.last().blend != blend ||
        m_batches.last().affectedByScreenTone != affectedByScreenTone ||
        m_batches.last().space != space || m_batches.last().depth != depth) {
        DrawBatch nova;
        nova.texture = texture;
        nova.blend = blend;
        nova.affectedByScreenTone = affectedByScreenTone;
        nova.space = space;
        nova.depth = depth;
        m_batches.push_back(nova);
    }
    QVector<QuadVertex>& v = m_batches.last().verts;
    const float x0 = float(dst.left()), y0 = float(dst.top());
    const float x1 = float(dst.right()), y1 = float(dst.bottom());
    const QuadVertex tl{ x0, y0, u0, v0, r, g, b, a };
    const QuadVertex tr{ x1, y0, u1, v0, r, g, b, a };
    const QuadVertex bl{ x0, y1, u0, v1, r, g, b, a };
    const QuadVertex br{ x1, y1, u1, v1, r, g, b, a };
    v << tl << tr << bl << tr << br << bl;      // dois triângulos
}

void SpriteBatcher::addTransformed(const QString& texture, const QTransform& t,
                                   const QRectF& local, const QRectF& src,
                                   const QSize& texSize, double opacity, PictureBlend blend,
                                   bool affectedByScreenTone, RuntimeCoordinateSpace space,
                                   double depth, bool smooth)
{
    if (local.isEmpty() || texSize.isEmpty()) return;
    // Aqui NÃO entra o recuo de 1/100 de texel: a imagem de uma picture é uma
    // textura inteira, não um recorte de atlas — não existe tile vizinho para
    // vazar, e recuar borraria a borda quando ela é escalada.
    const float u0 = float(src.left() / texSize.width());
    const float v0 = float(src.top() / texSize.height());
    const float u1 = float(src.right() / texSize.width());
    const float v1 = float(src.bottom() / texSize.height());
    const float a = float(qBound(0.0, opacity, 1.0));
    // ARMADILHA MEDIDA: o shader faz cor = textura × cor-do-vértice, e com
    // mistura aditiva o fator de origem é `One` — ou seja, o alfa do vértice
    // NÃO entraria na conta e a imagem somaria com opacidade cheia (medi 46 mil
    // pixels de diferença contra a CPU). Pré-multiplicando o alfa NA COR do
    // vértice, os três modos de mistura passam a fazer a conta certa.
    const float rgb = (blend == PictureBlend::Normal) ? 1.0f : a;

    // Cada quad transformado começa o seu lote: a mistura é estado de
    // pipeline e a ordem de desenho tem que ser respeitada.
    DrawBatch nova;
    nova.texture = texture;
    nova.blend = blend;
    nova.affectedByScreenTone = affectedByScreenTone;
    nova.smooth = smooth;
    nova.space = space;
    nova.depth = depth;
    m_batches.push_back(nova);
    QVector<QuadVertex>& v = m_batches.last().verts;

    const QPointF p00 = t.map(local.topLeft());
    const QPointF p10 = t.map(local.topRight());
    const QPointF p01 = t.map(local.bottomLeft());
    const QPointF p11 = t.map(local.bottomRight());
    const QuadVertex tl{ float(p00.x()), float(p00.y()), u0, v0, rgb, rgb, rgb, a };
    const QuadVertex tr{ float(p10.x()), float(p10.y()), u1, v0, rgb, rgb, rgb, a };
    const QuadVertex bl{ float(p01.x()), float(p01.y()), u0, v1, rgb, rgb, rgb, a };
    const QuadVertex br{ float(p11.x()), float(p11.y()), u1, v1, rgb, rgb, rgb, a };
    v << tl << tr << bl << tr << br << bl;
}

void SpriteBatcher::addSolid(const QRectF& dst, const QColor& cor, bool affectedByScreenTone,
                             RuntimeCoordinateSpace space)
{
    // Textura "branca" de 1x1: o mesmo shader serve para cor sólida.
    add(QStringLiteral("solid"), dst, QRectF(0, 0, 1, 1), QSize(1, 1),
        cor.alphaF(), cor, PictureBlend::Normal, affectedByScreenTone, space);
}

void SpriteBatcher::addSolidQuad(const QPolygonF& quad, const QColor& cor,
                                 bool affectedByScreenTone, RuntimeCoordinateSpace space)
{
    if (quad.size() != 4) return;
    const float a = float(qBound(0.0, cor.alphaF(), 1.0));
    const float r = float(cor.redF()), g = float(cor.greenF()), b = float(cor.blueF());
    const QString texture = QStringLiteral("solid");
    if (m_batches.isEmpty() || m_batches.last().texture != texture ||
        m_batches.last().blend != PictureBlend::Normal ||
        m_batches.last().affectedByScreenTone != affectedByScreenTone ||
        m_batches.last().space != space || m_batches.last().depth != 0.0) {
        DrawBatch nova;
        nova.texture = texture;
        nova.blend = PictureBlend::Normal;
        nova.affectedByScreenTone = affectedByScreenTone;
        nova.space = space;
        m_batches.push_back(nova);
    }
    auto vertex = [r,g,b,a](const QPointF& p) {
        return QuadVertex{float(p.x()), float(p.y()), 0.5f, 0.5f, r, g, b, a};
    };
    // Contrato: TL,TR,BR,BL. Mantemos winding consistente com add().
    const QuadVertex tl = vertex(quad.at(0));
    const QuadVertex tr = vertex(quad.at(1));
    const QuadVertex br = vertex(quad.at(2));
    const QuadVertex bl = vertex(quad.at(3));
    QVector<QuadVertex>& v = m_batches.last().verts;
    v << tl << tr << bl << tr << br << bl;
}

void addSpriteQuad(SpriteBatcher& out, ImageProvider& provider,
                   const QString& id, const QImage& img, const QRectF& src,
                   const QRectF& dst, double opacity)
{
    if (img.isNull()) return;
    const QString key = TextureKey::image(id);
    if (!provider.contains(key)) provider.insert(key, img);
    out.add(key, dst, src, img.size(), opacity);
}

namespace {
struct OcclusionCellKey {
    int tileWidth = 0;
    int tileHeight = 0;
    qint64 offsetXMilli = 0;
    qint64 offsetYMilli = 0;
    int x = 0;
    int y = 0;
    bool operator==(const OcclusionCellKey& other) const noexcept {
        return tileWidth == other.tileWidth && tileHeight == other.tileHeight
            && offsetXMilli == other.offsetXMilli && offsetYMilli == other.offsetYMilli
            && x == other.x && y == other.y;
    }
};
size_t qHash(const OcclusionCellKey& key, size_t seed = 0) noexcept
{
    seed = ::qHash(key.tileWidth, seed);
    seed = ::qHash(key.tileHeight, seed);
    seed = ::qHash(key.offsetXMilli, seed);
    seed = ::qHash(key.offsetYMilli, seed);
    seed = ::qHash(key.x, seed);
    return ::qHash(key.y, seed);
}

struct OpaqueTileKey {
    qint64 imageCacheKey = 0;
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const OpaqueTileKey& other) const noexcept {
        return imageCacheKey == other.imageCacheKey && x == other.x && y == other.y
            && width == other.width && height == other.height;
    }
};
size_t qHash(const OpaqueTileKey& key, size_t seed = 0) noexcept
{
    seed = ::qHash(key.imageCacheKey, seed);
    seed = ::qHash(key.x, seed);
    seed = ::qHash(key.y, seed);
    seed = ::qHash(key.width, seed);
    return ::qHash(key.height, seed);
}

struct OcclusionMap{QHash<quintptr,int> index;QHash<OcclusionCellKey,int> topOpaque;};
OcclusionCellKey cellKey(const LayerPtr&l,int x,int y)
{
    // Preserva exatamente a antiga granularidade textual de 3 casas decimais.
    return {l->tileWidth, l->tileHeight,
            qRound64(double(l->offsetx) * 1000.0), qRound64(double(l->offsety) * 1000.0), x, y};
}
Cell effectiveCell(const ScenePass& pass,const LayerPtr& layer,int x,int y)
{
    if(!layer||!layer->inBounds(x,y))return {};
    return pass.cellResolver?pass.cellResolver(layer,x,y):layer->data2D[y][x];
}
bool opaqueTile(const Editor&ed,const TileRef&t)
{
    const Tileset*ts=ed.tilesetAt(t.tilesetIdx);
    if(!ts||!ts->contains(t.tx,t.ty)||ts->image.isNull())return false;
    if(animatedAutotileAt(*ts,t.tx,t.ty,true))return false;
    static QHash<OpaqueTileKey,bool>cache;
    const QRect r=ts->tileRect(t.tx,t.ty);
    const OpaqueTileKey k{ts->image.cacheKey(),r.x(),r.y(),r.width(),r.height()};
    auto it=cache.constFind(k);if(it!=cache.constEnd())return it.value();
    bool solid=true;
    if(ts->image.hasAlphaChannel())for(int y=r.top();y<=r.bottom()&&solid;++y){const QRgb*line=reinterpret_cast<const QRgb*>(ts->image.constScanLine(y));for(int x=r.left();x<=r.right();++x)if(qAlpha(line[x])<255){solid=false;break;}}
    if(cache.size()>100000)cache.clear();
    cache.insert(k,solid);
    return solid;
}
bool tilePasses(const Editor&ed,const ScenePass&pass,const TileRef&t){const Tileset*ts=ed.tilesetAt(t.tilesetIdx);const QPoint c=ts?canonicalAnimatedTile(*ts,t.tx,t.ty):QPoint(t.tx,t.ty);const bool priority=ed.tilePriority(t.tilesetIdx,c.x(),c.y())>0;if(pass.aboveLayers)return !pass.starTiles;return pass.starTiles?priority:!priority;}
PictureBlend layerBlend(const QString&mode){if(mode=="multiply")return PictureBlend::Multiply;if(mode=="screen"||mode=="lighten")return PictureBlend::Screen;if(mode=="add")return PictureBlend::Add;return PictureBlend::Normal;}
void addMasked(SpriteBatcher&out,const LayerPtr&mask,const ScenePass&pass,const QString&key,const QRectF&dst,const QRectF&src,const QSize&texSize,double alpha,PictureBlend blend,double depth){if(!mask||mask->type!=LayerType::Tile){out.add(key,dst,src,texSize,alpha,QColor(),blend,true,RuntimeCoordinateSpace::World,depth);return;}const int tw=qMax(1,mask->tileWidth),th=qMax(1,mask->tileHeight);const int x0=qMax(0,int(std::floor((dst.left()-mask->offsetx)/tw))),y0=qMax(0,int(std::floor((dst.top()-mask->offsety)/th))),x1=qMin(mask->cols-1,int(std::floor((dst.right()-.0001-mask->offsetx)/tw))),y1=qMin(mask->rows-1,int(std::floor((dst.bottom()-.0001-mask->offsety)/th)));for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){if(!mask->inBounds(x,y)||effectiveCell(pass,mask,x,y).isEmpty())continue;const QRectF part=dst.intersected(QRectF(x*tw+mask->offsetx,y*th+mask->offsety,tw,th));if(part.isEmpty())continue;const double rx=(part.left()-dst.left())/dst.width(),ry=(part.top()-dst.top())/dst.height(),rw=part.width()/dst.width(),rh=part.height()/dst.height();const QRectF srcPart(src.left()+src.width()*rx,src.top()+src.height()*ry,src.width()*rw,src.height()*rh);out.add(key,part,srcPart,texSize,alpha,QColor(),blend,true,RuntimeCoordinateSpace::World,depth);}}
void collectLayers(const Editor&ed,const QVector<LayerPtr>&nodes,double parentAlpha,const ScenePass&pass,OcclusionMap&occ,int&order,bool masked=false){for(const LayerPtr&n:nodes){if(!n||!n->visible)continue;const bool above=n->zMode==QLatin1String("above");if(!n->isContainer()&&above!=pass.aboveLayers)continue;const double alpha=parentAlpha*n->opacity;if(n->type==LayerType::Group){collectLayers(ed,n->children,alpha,pass,occ,order,masked);continue;}const int idx=order++;occ.index[quintptr(n.data())]=idx;if(n->type==LayerType::Tile&&alpha>=.999&&!masked&&!n->isMask&&(n->blendMode.isEmpty()||n->blendMode==QLatin1String("normal")||n->blendMode==QLatin1String("source-over"))){int x0=0,y0=0,x1=n->cols-1,y1=n->rows-1;if(pass.visible.isValid()&&!pass.visible.isNull()){x0=qMax(0,int(std::floor((pass.visible.left()-n->offsetx)/n->tileWidth)));y0=qMax(0,int(std::floor((pass.visible.top()-n->offsety)/n->tileHeight)));x1=qMin(n->cols-1,int(std::ceil((pass.visible.right()-n->offsetx)/n->tileWidth)));y1=qMin(n->rows-1,int(std::ceil((pass.visible.bottom()-n->offsety)/n->tileHeight)));}for(int y=y0;y<=y1&&y<n->data2D.size();++y)for(int x=x0;x<=x1&&x<n->data2D[y].size();++x){const Cell cell=effectiveCell(pass,n,x,y);for(int i=cell.size()-1;i>=0;--i)if(tilePasses(ed,pass,cell[i])&&opaqueTile(ed,cell[i])){occ.topOpaque[cellKey(n,x,y)]=idx;break;}}}if(n->isMask)collectLayers(ed,n->children,alpha,pass,occ,order,true);}}

void emitirCamada(const Editor& ed,const LayerPtr& layer,double alpha,const ScenePass& pass,SpriteBatcher& out,ImageProvider& provider,const OcclusionMap*occ,const LayerPtr&clipMask)
{
    if(!layer||!layer->visible||alpha<=.001)return;auto passes=[&](const TileRef&t){return tilePasses(ed,pass,t);};
    if(layer->type==LayerType::Tile){const int tw=layer->tileWidth,th=layer->tileHeight;int x0=0,y0=0,x1=layer->cols-1,y1=layer->rows-1;if(pass.visible.isValid()&&!pass.visible.isNull()){x0=qMax(0,int(std::floor((pass.visible.left()-layer->offsetx)/tw)));y0=qMax(0,int(std::floor((pass.visible.top()-layer->offsety)/th)));x1=qMin(layer->cols-1,int(std::ceil((pass.visible.right()-layer->offsetx)/tw)));y1=qMin(layer->rows-1,int(std::ceil((pass.visible.bottom()-layer->offsety)/th)));}const int layerIndex=occ?occ->index.value(quintptr(layer.data()),-1):-1;for(int y=y0;y<=y1&&y<layer->data2D.size();++y){if(y<0)continue;const QVector<Cell>&row=layer->data2D[y];for(int x=x0;x<=x1&&x<row.size();++x){if(x<0)continue;if(occ&&occ->topOpaque.value(cellKey(layer,x,y),-1)>layerIndex)continue;const Cell cell=effectiveCell(pass,layer,x,y);int first=0;if(alpha>=.999&&(layer->blendMode.isEmpty()||layer->blendMode=="normal"||layer->blendMode=="source-over"))for(int i=cell.size()-1;i>=0;--i)if(passes(cell[i])&&opaqueTile(ed,cell[i])){first=i;break;}for(int i=first;i<cell.size();++i){const TileRef&c=cell[i];if(!passes(c))continue;const Tileset*ts=ed.tilesetAt(c.tilesetIdx);if(!ts||ts->image.isNull()||!ts->contains(c.tx,c.ty))continue;const QString key=TextureKey::tileset(c.tilesetIdx);if(!provider.contains(key))provider.insert(key,ts->image);const QRectF dst(x*tw+layer->offsetx,y*th+layer->offsety,tw,th);const int priority=ed.tilePriority(c.tilesetIdx,c.tx,c.ty);const double depth=pass.depthSortedTiles?tilePriorityWorldDepth(dst.bottom(),priority,dst.height()):0.0;addMasked(out,clipMask,pass,key,dst,QRectF(animatedTileRect(*ts,c.tx,c.ty,pass.animationTimeMs,quint32((x*73856093)^(y*19349663)))),ts->image.size(),alpha,layerBlend(layer->blendMode),depth);}}}}
    else if(layer->type==LayerType::Object){for(const MapObject&o:layer->objects){if(!o.visible||o.tiles.isEmpty())continue;const double ox=o.x+layer->offsetx,oy=o.y+layer->offsety;if(pass.visible.isValid()&&!pass.visible.isNull()&&!QRectF(ox,oy,o.w,o.h).intersects(pass.visible))continue;const int sw=qMax(1,o.stampW),sh=qMax(1,o.stampH);const double cw=o.w/sw,ch=o.h/sh;for(int i=0;i<o.tiles.size();++i){const TileRef&t=o.tiles[i];if(!passes(t))continue;const Tileset*ts=ed.tilesetAt(t.tilesetIdx);if(!ts||ts->image.isNull()||!ts->contains(t.tx,t.ty))continue;const QString key=TextureKey::tileset(t.tilesetIdx);if(!provider.contains(key))provider.insert(key,ts->image);const QRectF dst(ox+(i%sw)*cw,oy+(i/sw)*ch,cw,ch);const int priority=ed.tilePriority(t.tilesetIdx,t.tx,t.ty);const double depth=pass.depthSortedTiles?tilePriorityWorldDepth(dst.bottom(),priority,dst.height()):0.0;addMasked(out,clipMask,pass,key,dst,QRectF(animatedTileRect(*ts,t.tx,t.ty,pass.animationTimeMs,quint32(qHash(o.id)^quint32(i)))),ts->image.size(),alpha,layerBlend(layer->blendMode),depth);}}}
    else if(layer->type==LayerType::Image){if(pass.starTiles||layer->image.isNull())return;const QRectF dst(layer->offsetx,layer->offsety,layer->image.width(),layer->image.height());if(pass.visible.isValid()&&!pass.visible.isNull()&&!dst.intersects(pass.visible))return;const QString key=TextureKey::image(QStringLiteral("layer:")+layer->id);if(!provider.contains(key))provider.insert(key,layer->image);addMasked(out,clipMask,pass,key,dst,QRectF(QPointF(0,0),QSizeF(layer->image.size())),layer->image.size(),alpha,layerBlend(layer->blendMode),0.0);}
}
void percorrer(const Editor&ed,const QVector<LayerPtr>&nodes,double parentAlpha,const ScenePass&pass,SpriteBatcher&out,ImageProvider&provider,const OcclusionMap*occ,const LayerPtr&clipMask={}){for(const LayerPtr&n:nodes){if(!n||!n->visible)continue;const bool above=n->zMode==QLatin1String("above");if(!n->isContainer()&&above!=pass.aboveLayers)continue;if(n->isMask){if(n->maskShowBase)emitirCamada(ed,n,parentAlpha*n->opacity,pass,out,provider,occ,clipMask);percorrer(ed,n->children,parentAlpha*n->opacity,pass,out,provider,occ,n);}else if(n->type==LayerType::Group)percorrer(ed,n->children,parentAlpha*n->opacity,pass,out,provider,occ,clipMask);else emitirCamada(ed,n,parentAlpha*n->opacity,pass,out,provider,occ,clipMask);}}
} // namespace

void buildSceneBatches(const Editor&ed,const ScenePass&pass,SpriteBatcher&out,ImageProvider&provider){if(pass.drawBackground){const MapInfo&info=ed.mapInfo();out.addSolid(QRectF(0,0,info.pixelWidth(),info.pixelHeight()),info.background);}OcclusionMap occ;int order=0;collectLayers(ed,ed.layers(),1.0,pass,occ,order);percorrer(ed,ed.layers(),1.0,pass,out,provider,&occ);}

} // namespace game
