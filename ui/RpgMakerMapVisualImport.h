#pragma once
#include "core/ProjectIO.h"
#include "core/TilesetOps.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <functional>
#include <algorithm>

namespace ui::rpgMakerVisual {
inline QJsonObject json(const QString& path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
inline QString asset(const QString& root, const QString& folder, const QString& name) {
    if(name.isEmpty() || name.contains("..") || QDir::isAbsolutePath(name)) return {};
    return QDir(root).filePath(folder+"/"+name);
}
inline bool restoreSource(core::Editor& ed, core::MapDoc& map, const QJsonObject& source, QString* error) {
    if(source.isEmpty()) return false;
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    QTemporaryFile file(directory.filePath("source-XXXXXX.ludo"));
    if(!file.open()) return false;
    file.write(QJsonDocument(source).toJson(QJsonDocument::Compact)); file.flush();
    core::Editor loaded;
    if(!core::io::loadProject(loaded,file.fileName(),error,core::io::ProjectLoadMode::ReadOnlyPreview) || loaded.docs.isEmpty()) return false;
    const core::MapDoc* found=nullptr;
    for(const auto& d:loaded.docs) if(d.rpgMakerMapId==map.rpgMakerMapId) {found=&d;break;}
    if(!found && loaded.docs.size()==1 && loaded.docs.first().rpgMakerMapId==0) found=&loaded.docs.first();
    if(!found) return false;
    QVector<int> remap;
    for(const auto& ts:loaded.tilesets) {
        int idx=-1;
        for(int i=0;i<ed.tilesets.size();++i) if(ed.tilesets[i].id==ts.id){idx=i;break;}
        if(idx<0){idx=ed.tilesets.size();ed.tilesets.push_back(ts);}
        remap.push_back(idx);
    }
    auto index=[&](int old){return old>=0 && old<remap.size()?remap[old]:-1;};
    core::MapDoc restored=*found;
    restored.layers.clear();
    std::function<void(const core::LayerPtr&)> fix=[&](const core::LayerPtr& l){
        for(auto& row:l->data2D)for(auto& cell:row)for(auto& t:cell)t.tilesetIdx=index(t.tilesetIdx);
        for(auto& o:l->objects)for(auto& t:o.tiles)t.tilesetIdx=index(t.tilesetIdx);
        for(const auto& child:l->children)fix(child);
    };
    for(const auto& layer:found->layers){auto copy=core::cloneLayer(layer,false);fix(copy);restored.layers.push_back(copy);}
    for(auto ws:loaded.wangSets){
        bool exists=false;for(const auto& old:ed.wangSets)if(old.id==ws.id){exists=true;break;}if(exists)continue;
        ws.iconTilesetIdx=index(ws.iconTilesetIdx);
        for(auto& color:ws.colors)color.iconTilesetIdx=index(color.iconTilesetIdx);
        QHash<QString,core::WangTileData> tiles;
        for(auto it=ws.tiles.cbegin();it!=ws.tiles.cend();++it){const int colon=it.key().indexOf(':');if(colon<0)continue;tiles.insert(QString::number(index(it.key().left(colon).toInt()))+it.key().mid(colon),it.value());}
        ws.tiles=tiles;ed.wangSets.push_back(ws);
    }
    for(const auto& a:loaded.autotiles){bool exists=false;for(const auto& old:ed.autotiles)if(old.id==a.id){exists=true;break;}if(!exists)ed.autotiles.push_back(a);}
    restored.id=map.id;restored.parentId=map.parentId;restored.rpgMakerMapId=map.rpgMakerMapId;restored.name=map.name;
    restored.dirty=false;map=restored;ed.reindexTilesetGids();return true;
}
inline void importRuntime(core::Editor& ed, core::MapDoc& map, const QString& root, const QJsonObject& manifest, QStringList& missing) {
    map.map.tileWidth=qMax(1,manifest.value("tileWidth").toInt(32));
    map.map.tileHeight=qMax(1,manifest.value("tileHeight").toInt(32));
    for(const auto& v:manifest.value("chunks").toArray()) {
        const auto c=v.toObject();const QString path=asset(root,"img/ludoMaps",c.value("file").toString());QImage image(path);
        if(image.isNull()){missing<<path;continue;}
        auto l=core::makeImageLayer(image,QStringLiteral("Cenário recuperado"),path,false);
        l->offsetx=c.value("x").toInt();l->offsety=c.value("y").toInt();l->zMode=c.value("plane").toString("below");map.layers.push_back(l);
    }
    QHash<QString,int> atlasCache;
    QHash<QString,QImage> sourceImages;
    for(const auto& v:manifest.value("dynamic").toArray()) {
        const auto c=v.toObject();const auto frames=c.value("frames").toArray();if(frames.isEmpty())continue;
        const QString path=asset(root,"img/ludoMaps",c.value("atlas").toString());
        QJsonObject descriptor;
        for (const QString& field : {QStringLiteral("frames"),QStringLiteral("priority"),QStringLiteral("fps"),QStringLiteral("loop"),QStringLiteral("pingPong"),QStringLiteral("synchronized")}) descriptor.insert(field,c.value(field));
        const QString key=path+QString::fromUtf8(QJsonDocument(descriptor).toJson(QJsonDocument::Compact));
        int idx=atlasCache.value(key,-1);
        if(idx<0){
            if (!sourceImages.contains(path)) sourceImages.insert(path,QImage(path));
            const QImage original=sourceImages.value(path);if(original.isNull()){missing<<path;continue;}
            const auto first=frames.first().toArray();if(first.size()<4)continue;
            const int w=first[2].toInt(),h=first[3].toInt();if(w<=0||h<=0||qint64(w)*h*frames.size()>16777216)continue;
            QImage image(w*frames.size(),h,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter p(&image);
            for(int f=0;f<frames.size();++f){const auto rect=frames[f].toArray();if(rect.size()<4)continue;p.drawImage(QRect(f*w,0,w,h),original,QRect(rect[0].toInt(),rect[1].toInt(),rect[2].toInt(),rect[3].toInt()));}p.end();
            auto ts=core::makeTileset(image,"Tile recuperado",w,h,0,0);ts.category="Recuperados RPG Maker";ts.setTilePriority(0,0,c.value("priority").toInt());
            if(frames.size()>1){core::AnimatedAutotile animation;animation.cols=animation.rows=1;animation.fps=c.value("fps").toDouble(6);animation.loop=c.value("loop").toBool(true);animation.pingPong=c.value("pingPong").toBool();animation.synchronized=c.value("synchronized").toBool(true);for(int f=0;f<frames.size();++f)animation.frameOrigins.push_back(QPoint(f,0));ts.animatedAutotiles.push_back(animation);}
            idx=ed.tilesets.size();ed.tilesets.push_back(ts);atlasCache.insert(key,idx);
        }
        auto layer=core::makeObjectLayer("Objeto recuperado");layer->opacity=c.value("opacity").toDouble(1);layer->blendMode=c.value("blendMode").toString("source-over");layer->zMode=c.value("mode").toString()=="above"?"above":"below";
        core::MapObject o;o.x=c.value("x").toDouble();o.y=c.value("y").toDouble();o.w=c.value("width").toDouble();o.h=c.value("height").toDouble();o.tiles={core::TileRef{idx,0,0}};layer->objects.push_back(o);map.layers.push_back(layer);
    }
    // Preserve collision of old compiled maps, even without authoring sources.
    const auto collision=manifest.value("collision").toArray();
    if(!collision.isEmpty()){
        QImage image(16,1,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        auto ts=core::makeTileset(image,"Colisão recuperada",1,1,0,0);ts.category="Recuperados RPG Maker";
        for(int i=1;i<16;++i)ts.setTileCollisionMask(i,0,i);
        int idx=ed.tilesets.size();ed.tilesets.push_back(ts);
        auto layer=core::makeTileLayer("Colisão recuperada",map.map.tileWidth,map.map.tileHeight,map.map.width,map.map.height);
        for(int y=0;y<map.map.height;++y)for(int x=0;x<map.map.width;++x){const int i=y*map.map.width+x;const int mask=i<collision.size()?collision[i].toInt()&15:0;if(mask)layer->data2D[y][x]={core::TileRef{idx,mask,0}};}
        layer->locked=true;map.layers.push_back(layer);
    }
    std::stable_partition(map.layers.begin(),map.layers.end(),[](const core::LayerPtr& layer){return layer->zMode!="above";});
    ed.reindexTilesetGids();
}

// Tiles nativos MV/MZ permanecem como referência: o JSON nativo é a fonte de
// verdade. As tabelas de quadrantes são lidas do core da própria engine sem
// executar JavaScript (rpg_core.js no MV, rmmz_core.js no MZ).
inline void importNative(core::MapDoc& map,const QString& root,const QJsonObject& rpgMakerMap,QStringList& missing) {
    QFile sets(QDir(root).filePath("data/Tilesets.json"));if(!sets.open(QIODevice::ReadOnly))return;
    const auto catalog=QJsonDocument::fromJson(sets.readAll()).array();const int setId=rpgMakerMap.value("tilesetId").toInt();
    if(setId<0||setId>=catalog.size())return;
    const auto definition=catalog[setId].toObject();const auto names=definition.value("tilesetNames").toArray();
    const auto flags=definition.value("flags").toArray();const auto data=rpgMakerMap.value("data").toArray();
    const int tile=map.map.tileWidth,w=map.map.width,h=map.map.height;
    if(tile<=0||qint64(w)*h*tile*tile>67108864){missing<<"Mapa nativo excede o limite de memória da prévia";return;}
    QVector<QImage> images;
    for(const auto& n:names){QString path=asset(root,"img/tilesets",n.toString()+".png");images.push_back(n.toString().isEmpty()?QImage():QImage(path));if(!n.toString().isEmpty()&&images.last().isNull())missing<<path;}
    const QString mzCorePath=QDir(root).filePath("js/rmmz_core.js");
    const QString mvCore=QDir(root).filePath("js/rpg_core.js");
    const bool mvProject=!QDir(root).entryList(QStringList{QStringLiteral("*.rpgproject")},QDir::Files).isEmpty();
    const QString corePath=mvProject?mvCore:(QFileInfo::exists(mzCorePath)?mzCorePath:mvCore);
    QFile js(corePath);QString code;if(js.open(QIODevice::ReadOnly))code=QString::fromUtf8(js.readAll());
    auto table=[&](const QString& name){QRegularExpression re("Tilemap\\."+name+"\\s*=\\s*(\\[[\\s\\S]*?\\]);");QString raw=re.match(code).captured(1);raw.remove(QRegularExpression("//[^\\n]*"));raw.replace(QRegularExpression(",\\s*\\]"),"]");return QJsonDocument::fromJson(raw.toUtf8()).array();};
    const auto floor=table("FLOOR_AUTOTILE_TABLE"),wall=table("WALL_AUTOTILE_TABLE"),waterfall=table("WATERFALL_AUTOTILE_TABLE");
    QHash<int,QImage> cache;bool missingTable=false;
    auto graphic=[&](int id){
        if(cache.contains(id))return cache.value(id);
        QImage out(tile,tile,QImage::Format_ARGB32_Premultiplied);out.fill(Qt::transparent);QPainter p(&out);
        int sheet=-1;QRect src;
        if(id<2048){sheet=id>=1536?4:5+id/256;src=QRect(((id/128%2)*8+id%8)*tile,(id%256/8%16)*tile,tile,tile);if(sheet>=0&&sheet<images.size())p.drawImage(out.rect(),images[sheet],src);}
        else {
            const int kind=(id-2048)/48,shape=(id-2048)%48,col=kind%8,row=kind/8;
            int bx=0,by=0;QJsonArray quadrants=floor;
            if(id<2816){sheet=0;if(kind<4){bx=kind<2?0:6;by=kind%2*3;}else{bx=col/4*8;by=row*6+(col/2%2)*3;if(kind%2){bx+=6;quadrants=waterfall;}}}
            else if(id<4352){sheet=1;bx=col*2;by=(row-2)*3;}
            else if(id<5888){sheet=2;bx=col*2;by=(row-6)*2;quadrants=wall;}
            else{sheet=3;bx=col*2;by=int((row-10)*2.5+(row%2?0.5:0));if(row%2)quadrants=wall;}
            if(shape>=quadrants.size())missingTable=true;
            else if(sheet<images.size()){
                const auto pieces=quadrants[shape].toArray();const int half=tile/2;
                for(int q=0;q<4&&q<pieces.size();++q){const auto point=pieces[q].toArray();if(point.size()<2)continue;const int px=point[0].toInt(),py=point[1].toInt();QRect dst(q%2*half,q/2*half,half,half);QRect source((bx*2+px)*half,(by*2+py)*half,half,half);
                    const bool isTable=sheet==1&&id<flags.size()&&(flags[id].toInt()&128);
                    if(isTable&&(py==1||py==5)){p.drawImage(dst,images[sheet],QRect((bx*2+(py==1?(4-px)%4:px))*half,(by*2+3)*half,half,half));dst.setY(dst.y()+half/2);source.setHeight(half/2);p.drawImage(dst,images[sheet],source);}else p.drawImage(dst,images[sheet],source);
                }
            }
        }
        p.end();cache.insert(id,out);return out;
    };
    const int chunkCells=qMax(1,512/tile);
    for(int cy=0;cy<h;cy+=chunkCells)for(int cx=0;cx<w;cx+=chunkCells){
        const int cw=qMin(chunkCells,w-cx),ch=qMin(chunkCells,h-cy);
        QImage lower(cw*tile,ch*tile,QImage::Format_ARGB32_Premultiplied),upper=lower;lower.fill(Qt::transparent);upper.fill(Qt::transparent);
        QPainter low(&lower),high(&upper);bool any=false;
        for(int y=0;y<ch;++y)for(int x=0;x<cw;++x){
            for(int z=0;z<4;++z){const qint64 index=(qint64(z)*h+cy+y)*w+cx+x;if(index>=data.size())continue;const int id=data[int(index)].toInt();if(id>0){any=true;const bool above=id<flags.size()&&(flags[id].toInt()&16);(above?high:low).drawImage(QPoint(x*tile,y*tile),graphic(id));}
                if(z==1){const qint64 shadowIndex=(qint64(4)*h+cy+y)*w+cx+x;const int bits=shadowIndex<data.size()?data[int(shadowIndex)].toInt():0;for(int q=0;q<4;++q)if(bits&(1<<q)){any=true;low.fillRect(QRect(x*tile+q%2*(tile/2),y*tile+q/2*(tile/2),tile/2,tile/2),QColor(0,0,0,128));}}
            }
        }
        low.end();high.end();if(!any)continue;
        QPainter compose(&lower);compose.drawImage(0,0,upper);compose.end();
        auto layer=core::makeImageLayer(lower,"Mapa nativo RPG Maker (referência)",QString(),true);layer->offsetx=cx*tile;layer->offsety=cy*tile;layer->locked=true;map.layers.push_back(layer);
    }
    if(missingTable)missing<<(QFileInfo(corePath).fileName()+": tabelas de autotiles ausentes ou não reconhecidas");
}
}
