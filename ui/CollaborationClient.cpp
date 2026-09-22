#include "CollaborationClient.h"
#include "ProjectManagerDialog.h"
#include "TeamRasterTransport.h"
#include "TeamConnection.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "core/ProjectIO.h"
#include "core/ResourceManager.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QSaveFile>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>
#include <QSet>
#include <QProgressDialog>
#include <QScopedValueRollback>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QUuid>
#include <utility>
#include <QHostAddress>
#include <QAbstractSocket>

namespace ui {
namespace {
QString portableAssetPath(const QString& raw);
struct PreparedTeamAtlases {
    QHash<QString,QString> sources;QSet<QString> backings;QStringList changed,paths;
    core::AssetDatabase database;QString error;
};
PreparedTeamAtlases prepareTeamAtlases(const QVector<core::Tileset>& tilesets,const QString& projectRoot,core::AssetDatabase database) {
    PreparedTeamAtlases result;const QDir root(projectRoot);
    QHash<QString,QString> nextSources;QSet<QString> nextBackings;QStringList toIndex,changed;
    QString* error=&result.error;
    auto prepare=[&]() -> bool {
    auto safeId=[](QString id){
        for(int i=0;i<id.size();++i){
            const QChar c=id.at(i);
            if(!c.isLetterOrNumber()&&c!=QLatin1Char('-')&&c!=QLatin1Char('_'))id[i]=QLatin1Char('_');
        }
        return id;
    };
    for(const auto& ts:tilesets){
        if(ts.id.trimmed().isEmpty()||ts.image.isNull())continue;
        QString chosen;bool backing=false;
        const QString original=portableAssetPath(ts.sourcePath);
        if(!original.isEmpty()&&ts.sourceTileX==0&&ts.sourceTileY==0&&!ts.combined&&!ts.chromaApplied){
            const QString absolute=root.filePath(original);QImage source(absolute);
            if(!source.isNull()&&source==ts.image)chosen=original;
        }
        if(chosen.isEmpty()){
            chosen=QStringLiteral("Assets/LUDO/Team/Tilesets/%1.png").arg(safeId(ts.id));backing=true;
            const QString absolute=root.filePath(chosen);QDir().mkpath(QFileInfo(absolute).absolutePath());
            QImage current(absolute);
            if(current.isNull()||current!=ts.image){
                QSaveFile file(absolute);
                if(!file.open(QIODevice::WriteOnly)||!ts.image.save(&file,"PNG")||!file.commit()){
                    if(error)*error=QObject::tr("Não foi possível preparar o atlas compartilhado %1.").arg(ts.name);
                    return false;
                }
                changed<<chosen;
            }
        }
        nextSources.insert(ts.id,chosen);if(backing)nextBackings.insert(ts.id);toIndex<<chosen;
    }
    toIndex.removeDuplicates();
    QString dbError;if(!toIndex.isEmpty()&&!database.synchronizePaths(root.path(),toIndex,&dbError)){
        if(error)*error=dbError;return false;
    }
        return true;
    };
    prepare();result.sources=nextSources;result.backings=nextBackings;result.paths=toIndex;result.changed=changed;result.database=database;
    return result;
}
QString safeTeamFolderName(QString name) {
    name=name.trimmed();
    if(name.isEmpty())name=QObject::tr("Projeto");
    static const QString invalid=QStringLiteral("<>:\"/\\|?*");
    for(QChar& ch:name)if(invalid.contains(ch) || ch.unicode()<32)ch=QLatin1Char('_');
    while(name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))name.chop(1);
    name=name.left(96).trimmed();
    return name.isEmpty()?QObject::tr("Projeto"):name;
}

QString portableAssetPath(const QString& raw) {
    QString path=QDir::cleanPath(QDir::fromNativeSeparators(raw.trimmed()));
    while(path.startsWith(QStringLiteral("./")))path.remove(0,2);
    if(path==QStringLiteral(".") || QDir::isAbsolutePath(path) || !path.startsWith(QStringLiteral("Assets/"),Qt::CaseInsensitive))return {};
    const auto parts=path.split(QLatin1Char('/'),Qt::SkipEmptyParts);
    for(const auto& part:parts)if(part==QStringLiteral(".."))return {};
    return parts.join(QLatin1Char('/'));
}
QJsonValue portable(const QJsonValue& value) {
    if(value.isArray()) { QJsonArray out; for(const auto& v:value.toArray())out.append(portable(v)); return out; }
    if(!value.isObject())return value;
    auto out=value.toObject();
    for(auto it=out.begin();it!=out.end();) {
        if(it.key()=="collapsed")it=out.erase(it);
        else if(it.key()=="source" || it.key()=="sourcePath" || it.key()=="panoramaPath") {
            const QString path=portableAssetPath(it.value().toString());
            if(path.isEmpty())it=out.erase(it);else {it.value()=path;++it;}
        } else {it.value()=portable(it.value());++it;}
    }
    return out;
}
QJsonObject assetRecordJson(const core::AssetRecord& record) {
    QJsonObject out{{"id",record.id},{"path",record.path},{"sha256",record.sha256},{"size",double(record.size)},
                    {"type",record.type},{"category",record.category}};
    if(!record.metadata.isEmpty())out["metadata"]=record.metadata;
    return out;
}
QHash<QString,QJsonObject> assetState(const core::AssetDatabase& database) {
    QHash<QString,QJsonObject> result;
    for(const auto& record:database.records())if(!record.missing&&!record.id.isEmpty()&&!record.sha256.isEmpty())result.insert(record.id,assetRecordJson(record));
    return result;
}
QString fileSha256(const QString& absolutePath) {
    QFile file(absolutePath);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buffer[128*1024];
    while(!file.atEnd()){const qint64 n=file.read(buffer,sizeof(buffer));if(n<0)return {};if(n==0)break;hash.addData(QByteArrayView(buffer,n));}
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject mapsById(const QJsonArray& maps) {
    QJsonObject result;for(const auto& m:maps){auto map=m.toObject();result.insert(map.value("id").toString(),map);}return result;
}
QJsonObject resourcesOnly(QJsonObject payload) {
    payload.remove("maps");
    return payload;
}
QJsonArray mapOrder(const QJsonObject& payload) {
    QJsonArray result;
    for(const auto& value:payload.value("maps").toArray())result.append(value.toObject().value("id").toString());
    return result;
}
QJsonObject teamMapPayload(const core::MapDoc& doc) {
    auto map=portable(core::io::buildProjectMapPayload(doc)).toObject();
    map.remove("activeLayerIdx");
    map.remove("activeLayerId");
    return map;
}

QJsonObject snapshotMap(const core::DocSnapshot& snapshot, const QString& id) {
    core::MapDoc doc;doc.id=id;doc.name=snapshot.name;doc.parentId=snapshot.parentId;
    doc.variationBaseId=snapshot.variationBaseId;doc.variationName=snapshot.variationName;
    doc.rpgMakerMapId=snapshot.rpgMakerMapId;doc.rpgMakerImported=snapshot.rpgMakerImported;
    doc.map=snapshot.map;doc.layers=snapshot.layers;doc.rpgMakerRegions=snapshot.rpgMakerRegions;
    doc.rpgMakerRegionsAuthored=snapshot.rpgMakerRegionsAuthored;doc.reflectionSettings=snapshot.reflectionSettings;
    return teamMapPayload(doc);
}
QJsonObject mapPropertyChanges(QJsonObject before, QJsonObject after) {
    QJsonObject changes;
    for(const auto& key:{"map","reflectionSettings"}) {
        if(before.value(key)!=after.value(key)){
            if(QString::fromLatin1(key)=="map"){
                const auto a=before.value(key).toObject(),b=after.value(key).toObject();QJsonObject patch;QSet<QString> keys;
                for(auto i=a.begin();i!=a.end();++i)keys.insert(i.key());for(auto i=b.begin();i!=b.end();++i)keys.insert(i.key());
                for(const auto& name:keys)if(a.value(name)!=b.value(name))patch[name]=b.contains(name)?b.value(name):QJsonValue(QJsonValue::Null);
                changes[key]=patch;
            }else changes[key]=after.value(key).toObject();
        }
    }
    const auto a=before.value("map").toObject(), b=after.value("map").toObject();
    for(const auto& key:{"width","height","tileWidth","tileHeight"})if(a.value(key)!=b.value(key))return {};
    for(const auto& key:{"map","reflectionSettings"}){before.remove(key);after.remove(key);}
    return before==after?changes:QJsonObject();
}
void resourceValuePatches(const QJsonValue& before,const QJsonValue& after,QJsonArray path,QJsonArray& out) {
    if(before==after)return;
    if(before.isObject()&&after.isObject()){
        const auto a=before.toObject(),b=after.toObject();QSet<QString> keys;
        for(auto it=a.begin();it!=a.end();++it)keys.insert(it.key());for(auto it=b.begin();it!=b.end();++it)keys.insert(it.key());
        for(const auto& key:keys){auto child=path;child.append(key);resourceValuePatches(a.value(key),b.value(key),child,out);}return;
    }
    if(before.isArray()&&after.isArray()){
        QHash<QString,QJsonObject> a,b;QStringList aOrder,bOrder;bool stable=true;
        auto index=[&stable](const QJsonArray& array,QHash<QString,QJsonObject>& indexed,QStringList& order){
            for(const auto& v:array){const auto item=v.toObject();const auto id=item.value("id").toString();
                if(!v.isObject()||id.isEmpty()||indexed.contains(id)){stable=false;return;}indexed[id]=item;order<<id;}
        };
        index(before.toArray(),a,aOrder);index(after.toArray(),b,bOrder);
        QStringList commonA,commonB;for(const auto& id:aOrder)if(b.contains(id))commonA<<id;
        for(const auto& id:bOrder)if(a.contains(id))commonB<<id;
        // New items can be appended independently. Explicit reordering uses a
        // checked collection replacement, never loses somebody else's change.
        QStringList expected=commonB;for(const auto& id:bOrder)if(!a.contains(id))expected<<id;
        if(stable&&commonA==commonB&&expected==bOrder){
            for(const auto& id:aOrder){auto child=path;child.append(QJsonObject{{"id",id}});
                resourceValuePatches(a.value(id),b.contains(id)?QJsonValue(b.value(id)):QJsonValue(QJsonValue::Undefined),child,out);}
            for(const auto& id:bOrder)if(!a.contains(id)){auto child=path;child.append(QJsonObject{{"id",id}});resourceValuePatches(QJsonValue(QJsonValue::Undefined),b.value(id),child,out);}return;
        }
    }
    QJsonObject op{{"kind",after.isUndefined()?"resource.delete":"resource.set"},{"path",path},{"beforeExists",!before.isUndefined()}};
    if(!before.isUndefined())op["before"]=before;if(!after.isUndefined())op["value"]=after;out.append(op);
}
void resourcePatches(const QJsonObject& before,const QJsonObject& after,QJsonArray path,QJsonArray& out){resourceValuePatches(before,after,path,out);}

// Project Operations (Protocol 9): metadados estruturais ficam separados do
// conteúdo pesado do mapa. Isso permite renomear/reparentear/vincular Map ID
// sem substituir layers inteiras nem depender de um versão salva manual.
QJsonObject teamMapMetadata(const QJsonObject& map) {
    QJsonObject meta;
    meta["name"]=map.value("name").toString();
    meta["parentId"]=map.value("parentId").toString();
    meta["variationBaseId"]=map.value("variationBaseId").toString();
    meta["variationName"]=map.value("variationName").toString();
    meta["rpgMakerMapId"]=map.value("rpgMakerMapId").toInt();
    meta["rpgMakerImported"]=map.value("rpgMakerImported").toBool(false);
    return meta;
}
QJsonObject teamMapWithoutMetadata(QJsonObject map) {
    map.remove("name");
    map.remove("parentId");
    map.remove("variationBaseId");
    map.remove("variationName");
    map.remove("rpgMakerMapId");
    map.remove("rpgMakerImported");
    return map;
}
bool isProjectStructureOperation(const QString& kind) {
    return kind==QStringLiteral("map.create") || kind==QStringLiteral("map.delete") ||
           kind==QStringLiteral("map.meta") || kind==QStringLiteral("map.order");
}
QJsonObject buildDelta(const QJsonObject& base,const QJsonObject& target) {
    const auto oldMaps=mapsById(base.value("maps").toArray());
    const auto newMaps=mapsById(target.value("maps").toArray());
    QJsonObject delta;QJsonArray changed,deleted;
    for(const auto& value:target.value("maps").toArray()) {
        const auto id=value.toObject().value("id").toString();
        if(oldMaps.value(id)!=value)changed.append(value);
    }
    for(const auto& value:base.value("maps").toArray()) {
        const auto id=value.toObject().value("id").toString();
        if(!newMaps.contains(id))deleted.append(id);
    }
    if(!changed.isEmpty())delta["maps"]=changed;
    if(!deleted.isEmpty())delta["deletedMaps"]=deleted;
    if(mapOrder(base)!=mapOrder(target))delta["mapOrder"]=mapOrder(target);
    const auto oldResources=resourcesOnly(base),newResources=resourcesOnly(target);
    if(oldResources!=newResources)delta["resources"]=newResources;
    return delta;
}
QJsonObject applyDelta(const QJsonObject& base,const QJsonObject& delta,bool* ok=nullptr) {
    bool valid=true;QJsonObject result=base;
    if(delta.contains("resources")) {
        if(!delta.value("resources").isObject())valid=false;
        else {const auto maps=result.value("maps").toArray();result=delta.value("resources").toObject();result["maps"]=maps;}
    }
    auto maps=mapsById(result.value("maps").toArray());
    if(delta.contains("deletedMaps")) {
        if(!delta.value("deletedMaps").isArray())valid=false;
        else for(const auto& value:delta.value("deletedMaps").toArray())maps.remove(value.toString());
    }
    if(delta.contains("maps")) {
        if(!delta.value("maps").isArray())valid=false;
        else for(const auto& value:delta.value("maps").toArray()) {
            const auto map=value.toObject();const auto id=map.value("id").toString();
            if(id.isEmpty())valid=false;else maps[id]=map;
        }
    }
    QJsonArray order=delta.contains("mapOrder")?delta.value("mapOrder").toArray():mapOrder(result);
    QSet<QString> used;QJsonArray rebuilt;
    for(const auto& value:order) {
        const auto id=value.toString();
        if(id.isEmpty()||used.contains(id)||!maps.contains(id)){valid=false;continue;}
        used.insert(id);rebuilt.append(maps.value(id));
    }
    if(used.size()!=maps.size())valid=false;
    if(valid)result["maps"]=rebuilt;
    if(ok)*ok=valid;
    return valid?result:QJsonObject();
}
QJsonObject teamTileRefJson(const core::TileRef& tile) {
    QJsonObject out{{"tilesetIdx",tile.tilesetIdx},{"tx",tile.tx},{"ty",tile.ty}};
    if(!tile.wangSetId.isEmpty()){out["wangSetId"]=tile.wangSetId;out["wangColorId"]=tile.wangColorId;}
    return out;
}
core::TileRef teamTileRefFromJson(const QJsonObject& value) {
    core::TileRef tile;tile.tilesetIdx=value.value("tilesetIdx").toInt(-1);tile.tx=value.value("tx").toInt();tile.ty=value.value("ty").toInt();
    tile.wangSetId=value.value("wangSetId").toString();tile.wangColorId=value.contains("wangColorId")?value.value("wangColorId").toInt(-1):-1;return tile;
}
QJsonObject teamObjectJson(const core::MapObject& object) {
    QJsonObject out{{"id",object.id},{"name",object.name},{"type",object.type},{"x",object.x},{"y",object.y},{"w",object.w},{"h",object.h},
        {"rotation",object.rotation},{"rotationFilter",object.rotationFilter},{"scaleFilter",object.scaleFilter},{"visible",object.visible},
        {"stampW",object.stampW},{"stampH",object.stampH}};
    QJsonArray tiles;for(const auto& tile:object.tiles)tiles.append(teamTileRefJson(tile));out["tiles"]=tiles;
    if(!object.properties.isEmpty()){QJsonObject props;for(auto it=object.properties.cbegin();it!=object.properties.cend();++it)props[it.key()]=it.value();out["properties"]=props;}
    return out;
}
core::MapObject teamObjectFromJson(const QJsonObject& value) {
    core::MapObject object;object.id=value.value("id").toString(object.id);object.name=value.value("name").toString();object.type=value.value("type").toString();
    object.x=value.value("x").toDouble();object.y=value.value("y").toDouble();object.w=value.value("w").toDouble();object.h=value.value("h").toDouble();
    object.rotation=value.value("rotation").toDouble();object.rotationFilter=value.value("rotationFilter").toString(QStringLiteral("rotsprite"));
    object.scaleFilter=value.value("scaleFilter").toString(QStringLiteral("nearest"));object.visible=value.value("visible").toBool(true);
    object.stampW=qMax(1,value.value("stampW").toInt(1));object.stampH=qMax(1,value.value("stampH").toInt(1));
    for(const auto& tileValue:value.value("tiles").toArray()){const auto tile=teamTileRefFromJson(tileValue.toObject());if(tile.isValid())object.tiles.push_back(tile);}
    const auto props=value.value("properties").toObject();for(auto it=props.constBegin();it!=props.constEnd();++it)object.properties[it.key()]=it.value().toString();
    return object;
}
QJsonObject teamLayerJson(const QJsonArray& nodes,const QString& id) {
    for(const auto& value:nodes){auto node=value.toObject();if(node.value("id").toString()==id)return node;const auto found=teamLayerJson(node.value("children").toArray(),id);if(!found.isEmpty())return found;}return {};
}
bool replaceTeamLayerJson(QJsonArray& nodes,const QString& id,const QJsonObject& replacement) {
    for(int i=0;i<nodes.size();++i){auto node=nodes.at(i).toObject();if(node.value("id").toString()==id){nodes[i]=replacement;return true;}auto children=node.value("children").toArray();if(replaceTeamLayerJson(children,id,replacement)){node["children"]=children;nodes[i]=node;return true;}}return false;
}
core::LayerPtr teamLayerPtr(const QVector<core::LayerPtr>& nodes,const QString& id) {
    for(const auto& node:nodes){if(!node)continue;if(node->id==id)return node;const auto found=teamLayerPtr(node->children,id);if(found)return found;}return {};
}
bool objectLayerMetadataSame(const core::LayerSnapshot& a,const core::LayerSnapshot& b) {
    return a.offsetx==b.offsetx && a.offsety==b.offsety && a.imageMaskEnabled==b.imageMaskEnabled && a.imageMask.cacheKey()==b.imageMask.cacheKey();
}

bool stableIdArray(const QJsonArray& array,QHash<QString,QJsonValue>* byId=nullptr,QStringList* order=nullptr) {
    QSet<QString> seen;
    for(const auto& value:array){
        if(!value.isObject())return false;
        const QString id=value.toObject().value("id").toString().trimmed();
        if(id.isEmpty()||seen.contains(id))return false;
        seen.insert(id);if(byId)(*byId)[id]=value;if(order)order->push_back(id);
    }
    return true;
}

bool mergeTeamJson(const QJsonValue& base,const QJsonValue& local,const QJsonValue& remote,QJsonValue* merged) {
    if(local==base){*merged=remote;return true;}
    if(remote==base||local==remote){*merged=local;return true;}
    if(local.isObject()&&remote.isObject()&&(base.isObject()||base.isUndefined())){
        const auto b=base.toObject(),l=local.toObject(),r=remote.toObject();QSet<QString> keys;
        for(auto it=b.constBegin();it!=b.constEnd();++it)keys.insert(it.key());
        for(auto it=l.constBegin();it!=l.constEnd();++it)keys.insert(it.key());
        for(auto it=r.constBegin();it!=r.constEnd();++it)keys.insert(it.key());
        QJsonObject out;
        for(const auto& key:keys){QJsonValue value;if(!mergeTeamJson(b.value(key),l.value(key),r.value(key),&value))return false;if(!value.isUndefined())out.insert(key,value);}
        *merged=out;return true;
    }
    if(local.isArray()&&remote.isArray()&&(base.isArray()||base.isUndefined())){
        const auto b=base.toArray(),l=local.toArray(),r=remote.toArray();
        QHash<QString,QJsonValue> bm,lm,rm;QStringList bo,lo,ro;
        if(!stableIdArray(b,&bm,&bo)||!stableIdArray(l,&lm,&lo)||!stableIdArray(r,&rm,&ro))return false;
        QSet<QString> ids;for(const auto& id:bo)ids.insert(id);for(const auto& id:lo)ids.insert(id);for(const auto& id:ro)ids.insert(id);
        QHash<QString,QJsonValue> combined;
        for(const auto& id:ids){QJsonValue value;if(!mergeTeamJson(bm.value(id),lm.value(id),rm.value(id),&value))return false;if(!value.isUndefined())combined[id]=value;}
        // A ordem remota é a base visual compartilhada; inclusões exclusivamente
        // locais são anexadas na ordem local. Reordenações concorrentes do mesmo
        // conjunto ficam protegidas pelo fallback de conflito em vez de tentar
        // inventar uma ordem nova.
        QStringList commonBase;for(const auto& id:bo)if(lm.contains(id)&&rm.contains(id))commonBase<<id;
        auto filtered=[&](const QStringList& source){QStringList out;for(const auto& id:source)if(commonBase.contains(id))out<<id;return out;};
        const auto localCommon=filtered(lo),remoteCommon=filtered(ro);
        const bool localReordered=localCommon!=commonBase,remoteReordered=remoteCommon!=commonBase;
        if(localReordered&&remoteReordered&&localCommon!=remoteCommon)return false;
        QStringList finalOrder=(localReordered&&!remoteReordered)?lo:ro;
        for(const auto& id:lo)if(!finalOrder.contains(id))finalOrder<<id;
        for(const auto& id:combined.keys())if(!finalOrder.contains(id))finalOrder<<id;
        QJsonArray out;for(const auto& id:finalOrder)if(combined.contains(id))out.append(combined.value(id));
        *merged=out;return true;
    }
    return false;
}

bool mergeTeamResources(const QJsonObject& base,const QJsonObject& local,const QJsonObject& remote,QJsonObject* merged) {
    QJsonValue value;if(!mergeTeamJson(base,local,remote,&value)||!value.isObject())return false;*merged=value.toObject();return true;
}

bool privateOrLoopbackHost(const QString& host) {
    const auto h=host.trimmed().toLower();
    if(h=="localhost" || h.endsWith(".localhost"))return true;
    QHostAddress address;if(!address.setAddress(h))return false;
    if(address.isLoopback()||address.isLinkLocal())return true;
    if(address.protocol()==QAbstractSocket::IPv4Protocol) {
        const quint32 ip=address.toIPv4Address();
        if((ip&0xff000000u)==0x0a000000u)return true;
        if((ip&0xfff00000u)==0xac100000u)return true;
        if((ip&0xffff0000u)==0xc0a80000u)return true;
        if((ip&0xffc00000u)==0x64400000u)return true; // Tailscale / CGNAT 100.64.0.0/10
    }
    if(address.protocol()==QAbstractSocket::IPv6Protocol) {
        const auto bytes=address.toIPv6Address();
        if((bytes[0]&0xfe)==0xfc)return true;
    }
    return false;
}
}
CollaborationClient::CollaborationClient(core::Editor& editor,QWidget* parent)
    : QObject(parent),ed(editor),window(parent),network(this),timer(this),roomFlushTimer(this),roomWatchRetryTimer(this),assetSyncTimer(this),resourceSyncTimer(this),mapStructureTimer(this),structureRefreshTimer(this) {
    timer.setInterval(20000);
    roomFlushTimer.setSingleShot(true);
    roomWatchRetryTimer.setSingleShot(true);
    assetSyncTimer.setSingleShot(true);
    resourceSyncTimer.setSingleShot(true);
    mapStructureTimer.setSingleShot(true);
    connection=new TeamConnection(this);
    rasterTransport=new TeamRasterTransport(this);
    structureRefreshTimer.setSingleShot(true);
    connect(&timer,&QTimer::timeout,this,&CollaborationClient::heartbeat);
    connect(&roomFlushTimer,&QTimer::timeout,this,&CollaborationClient::roomTick);
    connect(&roomWatchRetryTimer,&QTimer::timeout,this,&CollaborationClient::ensureRoomWatch);
    connect(&assetSyncTimer,&QTimer::timeout,this,&CollaborationClient::syncAssetsAsync);
    connect(&resourceSyncTimer,&QTimer::timeout,this,&CollaborationClient::syncResourcesAsync);
    connect(&mapStructureTimer,&QTimer::timeout,this,&CollaborationClient::reconcileMapStructure);
    connect(&structureRefreshTimer,&QTimer::timeout,this,&CollaborationClient::syncStructureAsync);
    QSettings settings;
    // deviceId identifica a instalação; clientId identifica ESTA sessão/janela.
    // Duas cópias do LUDO executadas no mesmo usuário do Windows compartilham
    // QSettings, portanto um clientId persistente fazia uma janela tratar as
    // operações da outra como se fossem próprias.
    QString deviceId=settings.value("collaboration/deviceId").toString();
    if(deviceId.isEmpty()) {
        // Migra silenciosamente instalações que já tinham o identificador antigo.
        deviceId=settings.value("collaboration/clientId").toString();
        if(deviceId.isEmpty())deviceId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("collaboration/deviceId",deviceId);
    }
    clientId=deviceId+QStringLiteral(":")+QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Protocolo 9: alterações de mapa nascem do mesmo histórico transacional
    // usado pelo Undo/Redo e são enviadas por evento, sem esperar um ciclo fixo.
    connect(&ed,&core::Editor::historyChanged,this,&CollaborationClient::captureHistoryChange);
    const auto globalResourceChanged=[this]{
        ++atlasPreparationRevision;atlasPreparationReady=false;
        if(!attached()||applyingRemote)return;
        syncState.resourceConflict=false;resourceDirty=true;scheduleResourceSync();
    };
    connect(&ed,&core::Editor::tilesetsChanged,this,globalResourceChanged);
    connect(&ed,&core::Editor::wangChanged,this,globalResourceChanged);
    connect(&ed,&core::Editor::patternsChanged,this,globalResourceChanged);
    connect(&ed.resources(),&core::ResourceManager::assetsChanged,this,[this](const QStringList& paths){
        if(!attached()||applyingRemote)return;
        ++assetIndexRevision;assetIndexReady=false;
        assetDirty=true;if(paths.isEmpty())assetFullScanRequested=true;scheduleAssetSync(paths);
    });
    connect(&ed,&core::Editor::docsChanged,this,[this]{
        if(!attached())return;
        restartRoomWatch();
        if(applyingRemote)return;
        scheduleMapStructureReconcile(0);
    });
    connect(&ed,&core::Editor::projectChanged,this,[this]{
        if(!attached()||applyingRemote)return;
        scheduleMapStructureReconcile(0);
    });
}
bool CollaborationClient::attached() const {return !project.isEmpty() && ed.projectId==identity && ed.projectPath==localPath;}
bool CollaborationClient::hasPending() const {
    // Para a UI/atualização do RPG Maker, uma transferência já iniciada continua sendo
    // trabalho pendente até receber o ACK. Antes, a atualização do RPG Maker podia enxergar
    // a fila como vazia no intervalo entre disparar o request e terminar o
    // callback de Asset/Resource Sync.
    syncState.operationsInFlight=rasterDownloading||assetIndexPending||atlasPreparationPending||resourceRequestPending||assetRequestPending||roomRequestPending;
    syncState.localChangesPending=resourceDirty||assetDirty||!pendingAssetOperations.isEmpty()||!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty();
    syncState.structureDirty=mapStructureTimer.isActive()||syncState.checkpointRequired;
    return attached() && (syncState.hasWork() || syncState.blocksPublication() ||
                          assetDirty || assetRequestPending || !pendingAssetOperations.isEmpty() ||
                          !pendingRoomOperations.isEmpty() || roomRequestPending);
}
void CollaborationClient::markTeamPending() {
    // Sinais como mapChanged/docsChanged também são usados ao trocar de aba.
    // Só promovemos o hint quando o próprio Editor informa que existe uma
    // alteração persistente. Depois de promovido ele permanece verdadeiro
    // mesmo se o usuário fizer Ctrl+S localmente, até sincronizar com a equipe.
    if(attached()&&!applyingRemote&&ed.projectDirty)syncState.checkpointRequired=true;
}

bool CollaborationClient::mapKnownToServer(const QString& mapId) const {
    if(mapId.isEmpty())return false;
    for(const auto& value:serverBase.value("maps").toArray())
        if(value.toObject().value("id").toString()==mapId)return true;
    return false;
}

void CollaborationClient::scheduleMapStructureReconcile(int delayMs) {
    if(!attached()||applyingRemote)return;
    const int delay=qBound(0,delayMs,500);
    if(mapStructureTimer.isActive()&&mapStructureTimer.remainingTime()<=delay)return;
    mapStructureTimer.start(delay);
}

void CollaborationClient::reconcileMapStructure() {
    if(!attached()||applyingRemote)return;

    const auto baseMaps=mapsById(serverBase.value("maps").toArray());
    QSet<QString> currentIds;
    QHash<QString,QJsonObject> currentMaps;
    QJsonArray currentOrder;
    for(const auto& doc:ed.docs){
        currentIds.insert(doc.id);
        currentOrder.append(doc.id);
        currentMaps.insert(doc.id,teamMapPayload(doc));
    }

    if(role=="viewer"){
        if(currentOrder!=mapOrder(serverBase))syncState.checkpointRequired=true;
        restartRoomWatch();return;
    }

    // Remove apenas criações ainda NÃO enviadas que foram desfeitas antes de
    // chegar ao servidor. Uma criação já em voo precisa receber ACK primeiro;
    // na reconciliação seguinte ela vira map.delete, mantendo idempotência.
    QJsonArray cleaned;
    for(const auto& value:pendingRoomOperations){
        const auto op=value.toObject();
        if(op.value("kind").toString()=="map.create" &&
           op.value("id").toString().isEmpty() &&
           !currentIds.contains(op.value("map").toString()))continue;
        cleaned.append(op);
    }
    pendingRoomOperations=cleaned;

    // Novos mapas entram como uma operação estrutural explícita.
    for(const auto& doc:ed.docs){
        if(baseMaps.contains(doc.id))continue;
        bool updated=false;bool createInFlight=false;QJsonObject sentCreateValue;
        for(int i=0;i<pendingRoomOperations.size();++i){
            auto op=pendingRoomOperations.at(i).toObject();
            if(op.value("kind").toString()=="map.create"&&op.value("map").toString()==doc.id){
                if(op.value("id").toString().isEmpty()){op["value"]=currentMaps.value(doc.id);pendingRoomOperations[i]=op;}
                else {createInFlight=true;sentCreateValue=op.value("value").toObject();}
                updated=true;break;
            }
        }
        if(!updated)queueRoomOperation(QJsonObject{{"map",doc.id},{"kind","map.create"},{"value",currentMaps.value(doc.id)}});
        else if(createInFlight&&sentCreateValue!=currentMaps.value(doc.id)){
            // O POST do map.create já carrega uma cópia imutável. Edições
            // feitas enquanto ele está em voo precisam virar uma segunda
            // operação; alterar apenas o JSON da fila faria o ACK engolir
            // mudanças que o servidor nunca recebeu.
            queueRoomOperation(QJsonObject{{"map",doc.id},{"kind","map.replace"},{"value",currentMaps.value(doc.id)}});
        }
    }

    // Exclusão deixa de depender de versão salva manual. O servidor cria um
    // versão salva recuperável depois de aceitar esta operação.
    for(auto it=baseMaps.constBegin();it!=baseMaps.constEnd();++it){
        const QString mapId=it.key();if(currentIds.contains(mapId))continue;
        bool already=false;for(const auto& value:pendingRoomOperations){const auto op=value.toObject();if(op.value("map").toString()==mapId&&op.value("kind").toString()=="map.delete"){already=true;break;}}
        if(!already)queueRoomOperation(QJsonObject{{"map",mapId},{"kind","map.delete"}});
    }

    // Renomear, mover na árvore, variações e vínculo MapXXX são Project
    // Operations pequenas. Não substituímos layers para alterar um nome.
    for(const auto& doc:ed.docs){
        if(!baseMaps.contains(doc.id))continue;
        const QJsonObject current=currentMaps.value(doc.id),base=baseMaps.value(doc.id).toObject();
        bool deleting=false;bool hasContentPending=false;
        for(const auto& value:pendingRoomOperations){
            const auto op=value.toObject();if(op.value("map").toString()!=doc.id)continue;
            const QString kind=op.value("kind").toString();
            if(kind=="map.delete"){deleting=true;break;}
            if(kind=="tile.cells"||kind=="region.cells"||kind=="objects.patch"||kind=="layer.replace"||kind=="map.properties"||kind=="map.replace")hasContentPending=true;
        }
        if(deleting)continue;
        if(teamMapMetadata(current)!=teamMapMetadata(base))
            queueRoomOperation(QJsonObject{{"map",doc.id},{"kind","map.meta"},{"value",teamMapMetadata(current)}});

        // Algumas rotinas antigas (ex.: reconstrução global de Autotiles)
        // alteram vários mapas fora do HistoryEntry. Até todas elas migrarem
        // para diffs próprios, detectamos esse buraco por comparação e usamos
        // map.replace SOMENTE quando não existe uma operação de conteúdo já
        // pendente para o mapa. Assim não duplicamos uma pincelada normal.
        if(!hasContentPending && teamMapWithoutMetadata(current)!=teamMapWithoutMetadata(base)) {
            const auto properties=mapPropertyChanges(teamMapWithoutMetadata(base),teamMapWithoutMetadata(current));
            if(!properties.isEmpty())queueRoomOperation(QJsonObject{{"map",doc.id},{"kind","map.properties"},{"value",properties}});
            else queueRoomOperation(QJsonObject{{"map",doc.id},{"kind","map.replace"},{"baseSeq",double(roomCursors.value(doc.id,0))},{"value",current}});
        }
    }

    // A ordem da árvore é estado do projeto, não efeito colateral de UI.
    // Enviamos a ordem final explicitamente; o servidor valida que contém
    // exatamente os mapas existentes após creates/deletes do mesmo lote.
    if(!ed.docs.isEmpty() && currentOrder!=mapOrder(serverBase)){
        queueRoomOperation(QJsonObject{{"map",ed.docs.first().id},{"kind","map.order"},{"order",currentOrder}});
    }

    if(!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty())scheduleRoomFlush(30);
    restartRoomWatch();
}

void CollaborationClient::scheduleStructureRefresh(int delayMs) {
    if(!attached()||remoteRevision<=revision)return;
    const int delay=qBound(0,delayMs,1500);
    if(structureRefreshTimer.isActive()&&structureRefreshTimer.remainingTime()<=delay)return;
    structureRefreshTimer.start(delay);
}

void CollaborationClient::syncStructureAsync() {
    if(!attached()||structureRefreshPending||remoteRevision<=revision)return;
    if(busy||applyingRemote||syncState.checkpointRequired||syncState.roomConflict||!pendingRoomResets.isEmpty()||resourceDirty||resourceRequestPending||assetRequestPending||assetDirty||!pendingAssetOperations.isEmpty()||remoteAssetSeq>assetCursor||!pendingRoomOperations.isEmpty()){scheduleStructureRefresh(180);return;}
    structureRefreshPending=true;
    postJsonAsync("projects/"+project+"/sync",QJsonObject{{"revision",revision}},12000,
        [this](bool ok,int,const QJsonObject& response,const QString& reason){
            structureRefreshPending=false;if(!attached())return;
            if(!ok){emit statusChanged(tr("Estrutura da equipe reconectando — %1").arg(reason));scheduleStructureRefresh(900);return;}
            const QJsonObject delta=response.value("delta").toObject();

            // Resource Sync possui seu próprio cursor e, no Protocol 9, também
            // garante asset -> metadata. Não aplicamos um snapshot global aqui,
            // pois isso poderia regredir um mapa que já recebeu operações da
            // sala posteriores ao versão salva.
            if(delta.contains("resources") && delta.value("resources").toObject()!=resourcesOnly(serverBase)){
                scheduleResourceSync(0);scheduleStructureRefresh(220);return;
            }
            if((delta.contains("maps")&&!delta.value("maps").isArray())||
               (delta.contains("deletedMaps")&&!delta.value("deletedMaps").isArray())||
               (delta.contains("mapOrder")&&!delta.value("mapOrder").isArray())){
                emit statusChanged(tr("Atualização estrutural inválida — cópia local preservada"));return;
            }

            const QString activeId=ed.doc()?ed.doc()->id:QString();
            QSet<QString> deleted;
            for(const auto& value:delta.value("deletedMaps").toArray()){
                const QString id=value.toString();if(!id.isEmpty())deleted.insert(id);
            }
            QHash<QString,QJsonObject> incoming;
            for(const auto& value:delta.value("maps").toArray()){
                const auto map=value.toObject();const QString id=map.value("id").toString();
                if(id.isEmpty()){emit statusChanged(tr("Atualização estrutural inválida — mapa sem ID"));return;}
                incoming[id]=map;
            }

            // Validação/preparo é feito antes de mutar o Editor. Assim uma
            // resposta estrutural incompleta nunca deixa metade da alteração
            // aplicada apesar da mensagem "cópia local preservada".
            QSet<QString> resultingIds;for(const auto& doc:ed.docs)if(!deleted.contains(doc.id))resultingIds.insert(doc.id);
            QHash<QString,core::MapDoc> preparedNewMaps;
            for(auto it=incoming.constBegin();it!=incoming.constEnd();++it){
                if(resultingIds.contains(it.key()))continue;
                core::MapDoc map;QString loadError;
                if(!core::io::loadProjectMapPayload(ed,it.value(),&map,&loadError)){
                    emit statusChanged(tr("Novo mapa da equipe não pôde ser aberto — %1").arg(loadError));return;
                }
                resultingIds.insert(map.id);preparedNewMaps.insert(map.id,std::move(map));
            }
            if(resultingIds.isEmpty()){emit statusChanged(tr("A equipe tentou remover todos os mapas — cópia local preservada"));return;}
            // Metadados estruturais de mapas já existentes também chegam pelo
            // versão salva quando a alteração aconteceu em uma sala que não está
            // aberta neste Editor. Validamos as referências antes de tocar no
            // projeto e depois aplicamos SOMENTE os metadados, nunca o conteúdo
            // potencialmente mais antigo do snapshot.
            for(auto it=incoming.constBegin();it!=incoming.constEnd();++it){
                const QString id=it.key();const auto map=it.value();
                const QString name=map.value("name").toString().trimmed();
                const QString parentId=map.value("parentId").toString();
                const QString variationBaseId=map.value("variationBaseId").toString();
                const int rpgMakerMapId=map.value("rpgMakerMapId").toInt();
                if(name.isEmpty()||rpgMakerMapId<0||parentId==id||variationBaseId==id ||
                   (!parentId.isEmpty()&&!resultingIds.contains(parentId)) ||
                   (!variationBaseId.isEmpty()&&!resultingIds.contains(variationBaseId))){
                    emit statusChanged(tr("Metadados estruturais inválidos — cópia local preservada"));return;
                }
            }
            if(delta.contains("mapOrder")){
                QSet<QString> orderIds;for(const auto& value:delta.value("mapOrder").toArray()){
                    const QString id=value.toString();if(id.isEmpty()||orderIds.contains(id)||!resultingIds.contains(id)){emit statusChanged(tr("Ordem de mapas inválida — cópia local preservada"));return;}orderIds.insert(id);
                }
                if(orderIds!=resultingIds){emit statusChanged(tr("Ordem de mapas incompleta — cópia local preservada"));return;}
            }

            bool structureChanged=false,activeRemoved=false;
            QScopedValueRollback<bool> guard(applyingRemote,true);
            if(!deleted.isEmpty()){
                for(int i=ed.docs.size()-1;i>=0;--i){
                    if(!deleted.contains(ed.docs[i].id))continue;
                    if(ed.docs[i].id==activeId)activeRemoved=true;
                    roomCursors.remove(ed.docs[i].id);pendingRoomResets.remove(ed.docs[i].id);observedHistoryPtr.remove(ed.docs[i].id);observedHistoryRevision.remove(ed.docs[i].id);
                    ed.docs.remove(i);structureChanged=true;
                }
            }

            // Um versão salva pode conter snapshots de mapas já conhecidos que
            // são mais antigos que as operações live recebidas depois dele.
            // Para mapas existentes aplicamos apenas Project Operations
            // (nome/hierarquia/variação/Map ID); conteúdo continua sendo
            // responsabilidade exclusiva do cursor da sala.
            for(auto it=incoming.constBegin();it!=incoming.constEnd();++it){
                auto* existing=ed.mapById(it.key());if(!existing)continue;
                const auto map=it.value();
                const QString name=map.value("name").toString().trimmed();
                const QString parentId=map.value("parentId").toString();
                const QString variationBaseId=map.value("variationBaseId").toString();
                const QString variationName=map.value("variationName").toString();
                const int rpgMakerMapId=qMax(0,map.value("rpgMakerMapId").toInt());
                const bool imported=map.value("rpgMakerImported").toBool(false);
                if(existing->name!=name||existing->parentId!=parentId||existing->variationBaseId!=variationBaseId||
                   existing->variationName!=variationName||existing->rpgMakerMapId!=rpgMakerMapId||existing->rpgMakerImported!=imported){
                    existing->name=name;existing->parentId=parentId;existing->variationBaseId=variationBaseId;
                    existing->variationName=variationName;existing->rpgMakerMapId=rpgMakerMapId;existing->rpgMakerImported=imported;
                    existing->dirty=true;structureChanged=true;
                }
            }
            for(auto it=preparedNewMaps.begin();it!=preparedNewMaps.end();++it){
                observedHistoryPtr[it.key()]=it.value().historyPtr;observedHistoryRevision[it.key()]=it.value().historyRevision;
                ed.docs.push_back(std::move(it.value()));structureChanged=true;
            }

            if(delta.contains("mapOrder")){
                const auto order=delta.value("mapOrder").toArray();
                QHash<QString,core::MapDoc> byId;for(auto& doc:ed.docs)byId.insert(doc.id,std::move(doc));
                QVector<core::MapDoc> reordered;reordered.reserve(order.size());
                for(const auto& value:order)reordered.push_back(std::move(byId[value.toString()]));
                ed.docs=std::move(reordered);structureChanged=true;
            }
            int activeIndex=ed.mapIndexById(activeId);if(activeIndex<0)activeIndex=qBound(0,ed.activeDocIdx,int(ed.docs.size())-1);ed.activeDocIdx=activeIndex;

            auto mergeStructure=[&](QJsonObject& base){
                auto maps=mapsById(base.value("maps").toArray());
                for(const auto& id:deleted)maps.remove(id);
                for(auto it=incoming.constBegin();it!=incoming.constEnd();++it){
                    if(!maps.contains(it.key())){maps[it.key()]=it.value();continue;}
                    auto current=maps.value(it.key()).toObject();const auto meta=teamMapMetadata(it.value());
                    current["name"]=meta.value("name");
                    for(const QString& key:{QStringLiteral("parentId"),QStringLiteral("variationBaseId"),QStringLiteral("variationName")}){
                        const QString value=meta.value(key).toString();if(value.isEmpty())current.remove(key);else current[key]=value;
                    }
                    const int rpgId=meta.value("rpgMakerMapId").toInt();if(rpgId>0)current["rpgMakerMapId"]=rpgId;else current.remove("rpgMakerMapId");
                    if(meta.value("rpgMakerImported").toBool(false))current["rpgMakerImported"]=true;else current.remove("rpgMakerImported");
                    maps[it.key()]=current;
                }
                QJsonArray order=delta.contains("mapOrder")?delta.value("mapOrder").toArray():mapOrder(base);
                QSet<QString> used;QJsonArray rebuilt;
                for(const auto& value:order){const QString id=value.toString();if(maps.contains(id)&&!used.contains(id)){rebuilt.append(maps.value(id));used.insert(id);}}
                for(auto it=maps.constBegin();it!=maps.constEnd();++it)if(!used.contains(it.key()))rebuilt.append(it.value());
                base["maps"]=rebuilt;
            };
            mergeStructure(serverBase);mergeStructure(localBase);
            revision=response.value("revision").toInt(revision);remoteRevision=qMax(remoteRevision,revision);persistLink();
            if(structureChanged){
                // Estrutura remota deve sujar o PROJETO, não o mapa ativo por
                // acidente. Editor::markDirty() também limpa rpgMakerImported
                // do mapa atual, o que seria uma mutação local indevida.
                ed.projectDirty=true;emit ed.projectChanged();emit ed.docsChanged();
                if(activeRemoved){emit ed.layersChanged();emit ed.selectionChanged();emit ed.mapChanged();}
            }
            restartRoomWatch();
            emit statusChanged(tr("Estrutura do projeto atualizada em tempo real"));
        });
}

QJsonObject CollaborationClient::snapshot() const {
    auto payload=portable(core::io::buildProjectPayload(ed)).toObject();
    for(const auto& field:{"activeMapDocIdx","rpgMakerProjectRoot","assetDatabase","assetReferences","editorVersion"})payload.remove(field);

    // Team Protocol 9: o PNG do Tileset não viaja mais escondido em `src`
    // quando existe uma fonte compartilhável exata. Para atlas derivados,
    // prepareTeamTilesetAssets() cria um backing PNG determinístico em Assets/.
    QJsonArray tilesets=payload.value("tilesets").toArray();
    for(int i=0;i<tilesets.size();++i){
        auto item=tilesets.at(i).toObject();const QString id=item.value("id").toString();
        const QString shared=portableAssetPath(teamTilesetSources.value(id));
        if(!shared.isEmpty()){
            item["source"]=shared;
            const QString assetId=ed.assetDatabase.idForPath(shared);
            if(!assetId.isEmpty())item["assetId"]=assetId;
            item.remove("src");
            if(teamTilesetBackingIds.contains(id)){item.remove("sourceTileX");item.remove("sourceTileY");}
            tilesets[i]=item;
        }
    }
    payload["tilesets"]=tilesets;

    QJsonArray maps;
    for(const auto& item:payload.value("maps").toArray()) {
        auto map=item.toObject();map.remove("activeLayerIdx");map.remove("activeLayerId");maps.append(map);
    }
    payload["maps"]=maps;return payload;
}
QJsonObject CollaborationClient::outgoing(const QJsonObject& current) const {
    // Preserve server fields that the local serializer normalizes on opening.
    auto result=serverBase;
    QSet<QString> keys;for(auto it=current.begin();it!=current.end();++it)keys.insert(it.key());
    for(auto it=localBase.begin();it!=localBase.end();++it)keys.insert(it.key());
    for(const auto& key:keys) {
        if(key=="maps" || current.value(key)==localBase.value(key))continue;
        if(current.contains(key))result[key]=current.value(key);else result.remove(key);
    }
    const auto oldMaps=mapsById(localBase.value("maps").toArray());
    const auto remoteMaps=mapsById(serverBase.value("maps").toArray());
    QJsonArray maps;
    for(const auto& item:current.value("maps").toArray()) {
        const auto map=item.toObject();const auto id=map.value("id").toString();
        maps.append(oldMaps.value(id)==item && remoteMaps.contains(id)?remoteMaps.value(id):item);
    }
    result["maps"]=maps;return result;
}
void CollaborationClient::setConnectionState(const QString& state) {
    TeamConnectionState next=TeamConnectionState::Offline;
    if(state=="online")next=TeamConnectionState::Online;else if(state=="slow")next=TeamConnectionState::Slow;
    else if(state=="reconnecting")next=TeamConnectionState::Connecting;else if(state=="attention")next=TeamConnectionState::Attention;
    syncState.reconnectRequired=next==TeamConnectionState::Offline||next==TeamConnectionState::Connecting;
    if(syncState.connection==next)return;syncState.connection=next;emit connectionStateChanged(syncState.connectionId());
}
void CollaborationClient::beginProgress(const QString& title,const QString& text) {
    if(progress)return;
    progress=new QProgressDialog(text,QString(),0,100,window);
    progress->setWindowTitle(title);
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(120);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    progress->show();
}
void CollaborationClient::updateProgress(int value,const QString& text) {
    if(!progress)return;
    if(!text.isEmpty())progress->setLabelText(text);
    progress->setValue(qBound(0,value,100));
    // repaint() garante feedback visual antes das etapas locais mais pesadas;
    // durante rede, o próprio event loop continua atualizando a janela.
    progress->repaint();
}
void CollaborationClient::endProgress() {
    if(!progress)return;
    progress->setValue(100);
    progress->close();
    progress->deleteLater();
    progress=nullptr;
}
bool CollaborationClient::request(const QString& path,const QJsonObject& body,QJsonObject& result,bool get,bool quiet,
                                  const QString& progressText,int progressStart,int progressEnd) {
    if(busy)return false;
    if(server.isEmpty()){if(!quiet)QMessageBox::information(window,tr("Equipe"),tr("Conecte ao servidor primeiro."));return false;}
    const bool ownsProgress=!quiet&&!progress;
    if(ownsProgress)beginProgress(tr("Modo de equipe"),progressText.isEmpty()?tr("Comunicando com o servidor…"):progressText);
    const bool trackProgress=progress && (!quiet || progressStart>=0 || progressEnd>=0 || !progressText.isEmpty());
    const int start=progressStart>=0?progressStart:(progress?progress->value():0);
    const int finish=progressEnd>=0?progressEnd:qMin(100,start+90);
    if(trackProgress&&!progressText.isEmpty())updateProgress(start,progressText);
    QScopedValueRollback<bool> guard(busy,true);
    if (syncState.connection==TeamConnectionState::Offline) setConnectionState(QStringLiteral("reconnecting"));
    QUrl url=server;url.setPath("/v1/"+path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    if(!token.isEmpty())req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    req.setTransferTimeout(quiet?5000:60000);
    QNetworkReply* reply=get?network.get(req):network.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    connect(&timeout,&QTimer::timeout,reply,&QNetworkReply::abort);
    connect(reply,&QNetworkReply::downloadProgress,reply,[this,reply,start,finish,trackProgress](qint64 received,qint64 total){
        if(received>160LL*1024*1024 || total>160LL*1024*1024){reply->abort();return;}
        if(trackProgress&&progress&&total>0)updateProgress(start+int((finish-start)*double(received)/double(total)));
    });
    connect(reply,&QNetworkReply::uploadProgress,reply,[this,start,finish,trackProgress](qint64 sent,qint64 total){
        if(trackProgress&&progress&&total>0)updateProgress(start+int((finish-start)*double(sent)/double(total)));
    });
    timeout.start(quiet?8000:90000);loop.exec(QEventLoop::ExcludeUserInputEvents);
    const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error=reply->error();const QString transport=reply->errorString();
    const auto document=QJsonDocument::fromJson(reply->readAll());result=document.object();reply->deleteLater();
    setConnectionState((status == 0 || status == 401) ? QStringLiteral("offline") : QStringLiteral("online"));
    if(error!=QNetworkReply::NoError || status!=200 || !document.isObject()) {
        const QString reason=result.value("error").toString(transport);
        emit statusChanged(tr("Não sincronizado — cópia local preservada"));
        if(!quiet)QMessageBox::warning(window,tr("Servidor"),reason+tr("\n\nSuas alterações locais não foram descartadas."));
        if(ownsProgress)endProgress();
        return false;
    }
    if(trackProgress&&progress)updateProgress(finish);
    if(ownsProgress)endProgress();
    return true;
}
void CollaborationClient::postJsonAsync(const QString& path,const QJsonObject& body,int timeoutMs,
                                              std::function<void(bool,int,const QJsonObject&,const QString&)> done) {
    if(server.isEmpty()){done(false,0,QJsonObject(),tr("Servidor indisponível."));return;}
    const quint64 requestEpoch=teamEpoch;
    connection->post(server,token,path,body,timeoutMs,requestEpoch,[this](quint64 epoch){return teamEpoch==epoch;},
        [this,done=std::move(done)](bool ok,int status,const QJsonObject& object,const QString& reason) mutable {
            if(status==401)setConnectionState(QStringLiteral("offline"));done(ok,status,object,reason);
        });
}
bool CollaborationClient::prepareTeamTilesetAssets(QString* error) {
    if(ed.projectRoot().trimmed().isEmpty())return true;
    const auto prepared=prepareTeamAtlases(ed.tilesets,ed.projectRoot(),ed.assetDatabase);
    if(!prepared.error.isEmpty()){if(error)*error=prepared.error;return false;}
    ed.assetDatabase=prepared.database;teamTilesetSources=prepared.sources;teamTilesetBackingIds=prepared.backings;
    if(assetState(ed.assetDatabase)!=sharedAssets){
        ++assetIndexRevision;assetIndexReady=false;assetDirty=true;
        scheduleAssetSync(prepared.paths,0);
    }
    return true;
}

void CollaborationClient::reloadTeamTilesetSources(const QStringList& changedPaths) {
    if(changedPaths.isEmpty())return;QSet<QString> paths;
    for(const auto& raw:changedPaths){const QString p=portableAssetPath(raw);if(!p.isEmpty())paths.insert(p.toLower());}
    bool changed=false;
    for(auto& ts:ed.tilesets){const QString rel=portableAssetPath(ts.sourcePath);if(rel.isEmpty()||!paths.contains(rel.toLower()))continue;
        QImage image(QDir(ed.projectRoot()).filePath(rel));if(image.isNull()||image==ts.image)continue;
        ts.image=image;ts.imagewidth=image.width();ts.imageheight=image.height();changed=true;
    }
    bool visuals=false;
    std::function<void(const QVector<core::LayerPtr>&)> reload=[&](const QVector<core::LayerPtr>& layers){
        for(const auto& layer:layers){
            if(!layer)continue;
            if(!layer->imagePaintLayer&&paths.contains(portableAssetPath(layer->imagePath).toLower())){
                QImage image(QDir(ed.projectRoot()).filePath(layer->imagePath));
                if(!image.isNull()&&image!=layer->image){layer->image=image;layer->imagewidth=image.width();layer->imageheight=image.height();visuals=true;}
            }
            reload(layer->children);
        }
    };
    for(auto& doc:ed.docs){reload(doc.layers);if(paths.contains(portableAssetPath(doc.map.panoramaPath).toLower())){
        QImage image(QDir(ed.projectRoot()).filePath(doc.map.panoramaPath));if(!image.isNull()&&image!=doc.map.panorama){doc.map.panorama=image;visuals=true;}
    }}
    if(changed){emit ed.tilesetsChanged();emit ed.wangChanged();}
    if(changed||visuals)emit ed.mapChanged();
}

QJsonObject CollaborationClient::currentResources() const {
    return resourcesOnly(snapshot());
}

void CollaborationClient::scheduleAssetSync(const QStringList& changedPaths,int delayMs) {
    if(!attached())return;
    for(const QString& raw:changedPaths){const QString path=portableAssetPath(raw);if(!path.isEmpty()&&!pendingAssetPaths.contains(path,Qt::CaseInsensitive))pendingAssetPaths.push_back(path);}
    const int delay=qBound(0,delayMs,2000);
    if(assetSyncTimer.isActive()&&assetSyncTimer.remainingTime()<=delay)return;
    assetSyncTimer.start(delay);
}

void CollaborationClient::syncAssetsAsync() {
    if(!attached()||assetRequestPending)return;
    if(busy||applyingRemote){scheduleAssetSync({},180);return;}

    const bool needsIndex=(assetFullScanRequested || (assetDirty&&!pendingAssetPaths.isEmpty()));
    if(needsIndex&&!assetIndexReady){
        if(assetIndexPending)return;
        assetIndexPending=true;
        const quint64 requestEpoch=teamEpoch;const QString requestedProject=project,requestedToken=token,root=ed.projectRoot();
        const auto original=ed.assetDatabase.toJson();auto database=ed.assetDatabase;
        const auto paths=pendingAssetPaths;const bool full=assetFullScanRequested;
        const quint64 generation=assetIndexRevision;
        using Result=QPair<core::AssetDatabase,QString>;
        auto* task=new QFutureWatcher<Result>(this);
        connect(task,&QFutureWatcher<Result>::finished,this,[this,task,requestEpoch,requestedProject,requestedToken,root,original,generation]{
            const auto result=task->result();task->deleteLater();
            if(teamEpoch!=requestEpoch||project!=requestedProject||token!=requestedToken||ed.projectRoot()!=root)return;
            assetIndexPending=false;
            if(!result.second.isEmpty()){emit statusChanged(tr("Leitura de recursos pendente — %1").arg(result.second));scheduleAssetSync({},900);return;}
            if(generation!=assetIndexRevision||ed.assetDatabase.toJson()!=original){scheduleAssetSync({},120);return;}
            ed.assetDatabase=result.first;assetIndexReady=true;
            for(const auto& move:ed.assetDatabase.takePathChanges())core::io::rewriteEditorAssetPath(ed,move.oldPath,move.newPath);
            scheduleAssetSync({},0);
        });
        task->setFuture(QtConcurrent::run([database,root,paths,full]() mutable {
            QString error;if(full)database.synchronize(root,&error);else database.synchronizePaths(root,paths,&error);
            return Result(database,error);
        }));
        return;
    }

    // Reconexão/atualização do RPG Maker preserva o estado local até conhecermos o manifesto
    // remoto. Fazemos a varredura uma única vez antes do bootstrap para que
    // arquivos alterados offline não sejam sobrescritos por um download antigo.
    if(!assetStateInitialized && preserveLocalAssetsOnBootstrap){
        QString error;
        if(assetFullScanRequested && !assetIndexReady){
            emit statusChanged(tr("Asset Sync pausado — %1").arg(error));scheduleAssetSync({},900);return;
        }
        assetFullScanRequested=false;
        bootstrapLocalAssetDatabase=ed.assetDatabase.toJson();
        bootstrapLocalAssets=assetState(ed.assetDatabase);
        bootstrapLocalKnownAssetIds.clear();bootstrapLocalMissingAssetIds.clear();
        for(const auto& record:ed.assetDatabase.records()){
            bootstrapLocalKnownAssetIds.insert(record.id);
            if(record.missing)bootstrapLocalMissingAssetIds.insert(record.id);
        }
    }
    assetRequestPending=true;

    if(assetDirty && assetStateInitialized && pendingAssetOperations.isEmpty()) {
        QString error;
        const QStringList paths=pendingAssetPaths;
        bool updated=true;
        if((assetFullScanRequested||!paths.isEmpty())&&!assetIndexReady)updated=false;
        if(!updated){assetRequestPending=false;emit statusChanged(tr("Asset Sync pausado — %1").arg(error));scheduleAssetSync({},900);return;}
        assetDirty=false;assetFullScanRequested=false;pendingAssetPaths.clear();assetIndexReady=false;
        if(role!="viewer") {
            const auto local=assetState(ed.assetDatabase);
            for(auto it=local.constBegin();it!=local.constEnd();++it) {
                if(!sharedAssets.contains(it.key())||sharedAssets.value(it.key())!=it.value())
                    pendingAssetOperations.append(QJsonObject{{"id",clientId+":asset:"+QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                                               {"client",clientId},{"kind","asset.upsert"},{"asset",it.value()}});
            }
            for(auto it=sharedAssets.constBegin();it!=sharedAssets.constEnd();++it)if(!local.contains(it.key()))
                pendingAssetOperations.append(QJsonObject{{"id",clientId+":asset:"+QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                                           {"client",clientId},{"kind","asset.delete"},{"assetId",it.key()}});
        }
    }

    QSet<QString> hashes;
    for(const auto& value:pendingAssetOperations){const auto op=value.toObject();if(op.value("kind").toString()=="asset.upsert")hashes.insert(op.value("asset").toObject().value("sha256").toString());}
    if(hashes.isEmpty()){sendAssetExchange();return;}
    QJsonArray list;for(const auto& hash:hashes)list.append(hash);
    postJsonAsync("projects/"+project+"/asset-blobs-check",QJsonObject{{"hashes",list}},8000,
        [this](bool ok,int,const QJsonObject& response,const QString& reason){
            if(!attached()){assetRequestPending=false;return;}
            if(!ok){assetRequestPending=false;emit statusChanged(tr("Asset Sync reconectando — %1").arg(reason));scheduleAssetSync({},850);return;}
            const QSet<QString> missing=[&]{QSet<QString> out;for(const auto& v:response.value("missing").toArray())out.insert(v.toString());return out;}();
            pendingAssetUploads.clear();pendingAssetUploadIndex=0;
            QSet<QString> queued;
            for(const auto& value:pendingAssetOperations){const auto op=value.toObject();if(op.value("kind").toString()!="asset.upsert")continue;const auto asset=op.value("asset").toObject();const QString hash=asset.value("sha256").toString();if(missing.contains(hash)&&!queued.contains(hash)){pendingAssetUploads.push_back(asset);queued.insert(hash);}}
            uploadNextAssetBlob();
        });
}

void CollaborationClient::uploadNextAssetBlob() {
    if(!attached()){assetRequestPending=false;return;}
    if(pendingAssetUploadIndex>=pendingAssetUploads.size()){sendAssetExchange();return;}
    const quint64 requestEpoch=teamEpoch;
    const QJsonObject asset=pendingAssetUploads.at(pendingAssetUploadIndex);
    const QString hash=asset.value("sha256").toString();
    const QString relative=portableAssetPath(asset.value("path").toString());
    const QString absolute=QDir(ed.projectRoot()).filePath(relative);
    auto* file=new QFile(absolute);
    if(relative.isEmpty()||!file->open(QIODevice::ReadOnly)){delete file;assetRequestPending=false;assetDirty=true;emit statusChanged(tr("Asset Sync: arquivo local não encontrado — %1").arg(relative));return;}
    QUrl url=server;url.setPath("/v1/projects/"+project+"/asset-blobs/"+hash);
    QNetworkRequest req(url);req.setTransferTimeout(60000);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/octet-stream");req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    auto* reply=network.post(req,file);file->setParent(reply);
    connect(reply,&QNetworkReply::uploadProgress,this,[this,relative](qint64 sent,qint64 total){
        if(total>2LL*1024*1024)emit statusChanged(tr("Enviando recurso %1 — %2%").arg(QFileInfo(relative).fileName()).arg(total>0?int(100.0*sent/total):0));
    });
    QTimer::singleShot(70000,reply,[reply]{if(reply->isRunning())reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,hash,requestEpoch]{
        if(teamEpoch!=requestEpoch){reply->deleteLater();return;}
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto doc=QJsonDocument::fromJson(reply->readAll());
        const bool ok=reply->error()==QNetworkReply::NoError&&status==200;const QString reason=doc.object().value("error").toString(reply->errorString());reply->deleteLater();
        if(!ok){assetRequestPending=false;assetDirty=true;emit statusChanged(tr("Falha ao enviar recurso — %1").arg(reason));scheduleAssetSync({},900);return;}
        ++pendingAssetUploadIndex;uploadNextAssetBlob();
    });
}

void CollaborationClient::sendAssetExchange() {
    QJsonObject body{{"cursor",double(assetCursor)},{"operations",pendingAssetOperations}};
    postJsonAsync("projects/"+project+"/asset-exchange",body,12000,
        [this](bool ok,int status,const QJsonObject& response,const QString& reason){
            if(!attached()){assetRequestPending=false;return;}
            if(!ok){assetRequestPending=false;if(status==403&&role=="viewer")assetDirty=false;else {emit statusChanged(tr("Asset Sync reconectando — %1").arg(reason));scheduleAssetSync({},850);}return;}
            applyAssetExchangeResponse(response);
        });
}

void CollaborationClient::applyAssetExchangeResponse(const QJsonObject& response) {
    QSet<QString> acked;for(const auto& value:response.value("acked").toArray())acked.insert(value.toString());
    if(!acked.isEmpty()){QJsonArray keep;for(const auto& value:pendingAssetOperations)if(!acked.contains(value.toObject().value("id").toString()))keep.append(value);pendingAssetOperations=keep;}
    QHash<QString,QJsonObject> next=sharedAssets;
    if(response.value("assetReset").toBool(false)) {
        next.clear();for(const auto& value:response.value("resetAssets").toArray()){const auto asset=value.toObject();next.insert(asset.value("id").toString(),asset);}
    }
    for(const auto& value:response.value("operations").toArray()){
        const auto op=value.toObject().value("operation").toObject();const QString kind=op.value("kind").toString();
        if(kind=="asset.upsert"){const auto asset=op.value("asset").toObject();next[asset.value("id").toString()]=asset;}
        else if(kind=="asset.delete")next.remove(op.value("assetId").toString());
    }
    // Só avançamos o cursor depois que todos os binários necessários forem
    // instalados com hash validado. Se a rede cair no meio de um download, a
    // próxima tentativa recebe novamente as mesmas operações em vez de pular
    // um asset que ainda não existe neste computador.
    pendingAssetCursor=qint64(response.value("assetSeq").toDouble(assetCursor));
    remoteAssetSeq=qMax(remoteAssetSeq,pendingAssetCursor);

    QStringList changedPaths;
    for(auto it=sharedAssets.constBegin();it!=sharedAssets.constEnd();++it){
        const auto old=it.value();
        if(next.contains(it.key())){
            const QString oldPath=portableAssetPath(old.value("path").toString());
            const QString newPath=portableAssetPath(next.value(it.key()).value("path").toString());
            if(!oldPath.isEmpty()&&oldPath.compare(newPath,Qt::CaseInsensitive)!=0){
                const QString absolute=QDir(ed.projectRoot()).filePath(oldPath);
                if(QFileInfo(absolute).isFile()&&fileSha256(absolute)==old.value("sha256").toString())QFile::remove(absolute);
                changedPaths<<oldPath;
            }
            continue;
        }
        const QString rel=portableAssetPath(old.value("path").toString());const QString absolute=QDir(ed.projectRoot()).filePath(rel);
        if(!rel.isEmpty()&&QFileInfo(absolute).isFile()&&fileSha256(absolute)==old.value("sha256").toString())QFile::remove(absolute);
        if(!rel.isEmpty())changedPaths<<rel;
    }
    pendingAssetDownloads.clear();pendingAssetDownloadIndex=0;pendingAssetNextState=next;
    for(auto it=next.constBegin();it!=next.constEnd();++it){
        const auto asset=it.value();const QString rel=portableAssetPath(asset.value("path").toString());if(rel.isEmpty())continue;
        const QString absolute=QDir(ed.projectRoot()).filePath(rel);const QString expected=asset.value("sha256").toString();
        const bool manifestChanged=!sharedAssets.contains(it.key())||sharedAssets.value(it.key())!=asset;
        const bool preserveLocal=!assetStateInitialized && preserveLocalAssetsOnBootstrap &&
            bootstrapLocalKnownAssetIds.contains(it.key()) &&
            (bootstrapLocalMissingAssetIds.contains(it.key()) || bootstrapLocalAssets.value(it.key())!=asset);
        if(preserveLocal){changedPaths<<rel;continue;}
        if(!QFileInfo(absolute).isFile()||fileSha256(absolute)!=expected)pendingAssetDownloads.push_back(asset);
        else if(manifestChanged)changedPaths<<rel;
    }
    pendingAssetPaths.append(changedPaths);pendingAssetPaths.removeDuplicates();
    downloadNextAssetBlob();
}

void CollaborationClient::downloadNextAssetBlob() {
    if(!attached()){assetRequestPending=false;return;}
    if(pendingAssetDownloadIndex>=pendingAssetDownloads.size()){finishAssetDownloads();return;}
    const QJsonObject asset=pendingAssetDownloads.at(pendingAssetDownloadIndex);const QString hash=asset.value("sha256").toString();const QString rel=portableAssetPath(asset.value("path").toString());
    const quint64 requestEpoch=teamEpoch;
    QUrl url=server;url.setPath("/v1/projects/"+project+"/asset-blobs/"+hash);QNetworkRequest req(url);req.setTransferTimeout(60000);req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    auto* reply=network.get(req);QTimer::singleShot(70000,reply,[reply]{if(reply->isRunning())reply->abort();});
    connect(reply,&QNetworkReply::downloadProgress,this,[this,rel](qint64 got,qint64 total){if(total>2LL*1024*1024)emit statusChanged(tr("Baixando recurso %1 — %2%").arg(QFileInfo(rel).fileName()).arg(total>0?int(100.0*got/total):0));});
    connect(reply,&QNetworkReply::finished,this,[this,reply,asset,hash,rel,requestEpoch]{
        if(teamEpoch!=requestEpoch){reply->deleteLater();return;}
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const QByteArray bytes=reply->readAll();const bool ok=reply->error()==QNetworkReply::NoError&&status==200&&QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==hash;const QString reason=reply->errorString();reply->deleteLater();
        if(!ok){assetRequestPending=false;emit statusChanged(tr("Falha ao baixar recurso %1 — %2").arg(QFileInfo(rel).fileName(),reason));scheduleAssetSync({},900);return;}
        const QString absolute=QDir(ed.projectRoot()).filePath(rel);QDir().mkpath(QFileInfo(absolute).absolutePath());QSaveFile file(absolute);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){assetRequestPending=false;emit statusChanged(tr("Não foi possível instalar o recurso %1.").arg(rel));scheduleAssetSync({},900);return;}
        pendingAssetPaths<<rel;++pendingAssetDownloadIndex;downloadNextAssetBlob();
    });
}

void CollaborationClient::finishAssetDownloads() {
    sharedAssets=pendingAssetNextState;pendingAssetNextState.clear();
    assetCursor=qMax(assetCursor,pendingAssetCursor);pendingAssetCursor=assetCursor;
    QJsonArray manifest;QStringList allPaths;
    QStringList ids=sharedAssets.keys();ids.sort(Qt::CaseInsensitive);
    for(const QString& id:ids){const auto asset=sharedAssets.value(id);QJsonObject item=asset;item["missing"]=false;manifest.append(item);allPaths<<asset.value("path").toString();}
    QJsonObject dbJson{{"version",1},{"assets",manifest}};
    const bool preservingBootstrap=!assetStateInitialized && preserveLocalAssetsOnBootstrap && !bootstrapLocalAssetDatabase.isEmpty();
    if(preservingBootstrap){
        // O manifesto do servidor continua em sharedAssets; o AssetDatabase
        // local mantém também arquivos novos/alterados offline. O próximo ciclo
        // calcula o diff e os publica sem destruir o trabalho local.
        QHash<QString,QJsonObject> merged;
        for(const auto& value:manifest){const auto item=value.toObject();merged[item.value("id").toString()]=item;}
        for(const auto& value:bootstrapLocalAssetDatabase.value("assets").toArray()){
            const auto item=value.toObject();const QString id=item.value("id").toString();if(!id.isEmpty())merged[id]=item;
        }
        QJsonArray combined;QStringList mergedIds=merged.keys();mergedIds.sort(Qt::CaseInsensitive);
        for(const auto& id:mergedIds)combined.append(merged.value(id));
        dbJson=bootstrapLocalAssetDatabase;dbJson["assets"]=combined;
    }
    {QScopedValueRollback<bool> guard(applyingRemote,true);ed.assetDatabase.fromJson(dbJson);QString error;
        if(!preservingBootstrap)ed.assetDatabase.synchronizePaths(ed.projectRoot(),allPaths,&error);
        reloadTeamTilesetSources(pendingAssetPaths);
        ed.resources().notifyAssetsChanged(pendingAssetPaths);}
    pendingAssetPaths.clear();pendingAssetUploads.clear();pendingAssetDownloads.clear();pendingAssetUploadIndex=0;pendingAssetDownloadIndex=0;
    assetStateInitialized=true;preserveLocalAssetsOnBootstrap=false;bootstrapLocalAssets.clear();bootstrapLocalKnownAssetIds.clear();bootstrapLocalMissingAssetIds.clear();bootstrapLocalAssetDatabase={};assetRequestPending=false;
    remoteAssetSeq=qMax(remoteAssetSeq,assetCursor);
    if(!pendingAssetOperations.isEmpty()||assetDirty)scheduleAssetSync({},80);
    else emit statusChanged(tr("Recursos compartilhados sincronizados"));
    if(resourceDirty)scheduleResourceSync(0);
    if(!deferredRemoteResources.isEmpty()&&!syncState.checkpointRequired&&!resourceDirty&&pendingRoomOperations.isEmpty()){
        const auto resources=deferredRemoteResources;const qint64 seq=deferredRemoteResourceSeq;const int rev=deferredRemoteResourceRevision;
        deferredRemoteResources={};deferredRemoteResourceSeq=0;deferredRemoteResourceRevision=0;applyRemoteResources(resources,seq,rev);
    }
}

void CollaborationClient::scheduleResourceSync(int delayMs) {
    if(!attached())return;const int delay=qBound(0,delayMs,2000);
    if(resourceSyncTimer.isActive()&&resourceSyncTimer.remainingTime()<=delay)return;resourceSyncTimer.start(delay);
}

void CollaborationClient::syncResourcesAsync() {
    if(!attached()||resourceRequestPending||syncState.resourceConflict)return;
    if(busy||applyingRemote){scheduleResourceSync(160);return;}
    if(!atlasPreparationReady){
        if(atlasPreparationPending)return;
        atlasPreparationPending=true;
        const quint64 requestEpoch=teamEpoch;const QString requestedProject=project,requestedToken=token,root=ed.projectRoot();
        const auto database=ed.assetDatabase;const auto original=database.toJson();const auto tilesets=ed.tilesets;
        const quint64 generation=atlasPreparationRevision;
        auto* task=new QFutureWatcher<PreparedTeamAtlases>(this);
        connect(task,&QFutureWatcher<PreparedTeamAtlases>::finished,this,[this,task,requestEpoch,requestedProject,requestedToken,root,original,generation]{
            const auto prepared=task->result();task->deleteLater();
            if(teamEpoch!=requestEpoch||project!=requestedProject||token!=requestedToken||root!=ed.projectRoot())return;
            atlasPreparationPending=false;
            if(!prepared.error.isEmpty()){emit statusChanged(tr("Recursos globais pausados — %1").arg(prepared.error));scheduleResourceSync(900);return;}
            if(generation!=atlasPreparationRevision||original!=ed.assetDatabase.toJson()){scheduleResourceSync(120);return;}
            ed.assetDatabase=prepared.database;teamTilesetSources=prepared.sources;teamTilesetBackingIds=prepared.backings;
            atlasPreparationReady=true;
            if(assetState(ed.assetDatabase)!=sharedAssets){++assetIndexRevision;assetIndexReady=false;assetDirty=true;scheduleAssetSync(prepared.paths,0);}
            scheduleResourceSync(0);
        });
        task->setFuture(QtConcurrent::run([tilesets,root,database]{return prepareTeamAtlases(tilesets,root,database);}));
        return;
    }
    if(remoteAssetSeq>assetCursor||assetRequestPending||assetDirty||!pendingAssetOperations.isEmpty()){
        if(remoteAssetSeq>assetCursor)scheduleAssetSync({},0);
        scheduleResourceSync(160);return;
    }
    resourceRequestPending=true;QJsonObject body{{"cursor",double(resourceCursor)}};const bool sending=resourceDirty&&role!="viewer";
    if(sending){
        if(pendingResourceOperationId.isEmpty()){
            pendingResourceSnapshot=currentResources();
            pendingResourceOperationId=clientId+":resources:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        QJsonArray patches;resourcePatches(resourcesOnly(localBase),pendingResourceSnapshot,{},patches);
        body["baseSeq"]=double(resourceCursor);body["id"]=pendingResourceOperationId;body["client"]=clientId;body["operations"]=patches;
    }
    postJsonAsync("projects/"+project+"/resources-exchange",body,15000,
        [this,sending](bool ok,int status,const QJsonObject& response,const QString& reason){
            resourceRequestPending=false;if(!attached())return;
            if(!ok){if(status==403&&role=="viewer")resourceDirty=false;else {emit statusChanged(tr("Recursos globais reconectando — %1").arg(reason));scheduleResourceSync(900);}return;}
            remoteAssetSeq=qMax(remoteAssetSeq,qint64(response.value("assetSeq").toDouble(assetCursor)));
            const qint64 seq=qint64(response.value("resourceSeq").toDouble(resourceCursor));const int rev=response.value("revision").toInt(revision);
            // Metadados de Tileset/Autotile nunca são aplicados antes dos
            // binários que eles referenciam. Se um asset chegou enquanto esta
            // requisição estava em voo, buscamos o manifesto/blob primeiro e
            // repetimos a troca de recursos com o mesmo cursor/op id.
            if(remoteAssetSeq>assetCursor){
                scheduleAssetSync({},0);scheduleResourceSync(160);return;
            }
            if(response.value("conflict").toBool(false)){
                const QJsonObject remoteResources=response.value("resources").toObject();
                const QJsonObject baseResources=resourcesOnly(localBase);
                const QJsonObject localResources=currentResources();
                QJsonObject mergedResources;
                pendingResourceOperationId.clear();
                if(!remoteResources.isEmpty() && mergeTeamResources(baseResources,localResources,remoteResources,&mergedResources)){
                    // Conflitos em recursos diferentes (ex.: duas pessoas importando
                    // Tilesets distintos) são mesclados automaticamente. O servidor
                    // remoto vira a nova base e reenviamos apenas o estado mesclado.
                    const auto savedOps=pendingRoomOperations;
                    const bool savedAssetDirty=assetDirty;
                    const bool savedAssetFullScan=assetFullScanRequested;
                    const auto savedPaths=pendingAssetPaths;
                    const auto savedAssetDb=ed.assetDatabase.toJson();
                    const bool savedPendingHint=syncState.checkpointRequired;
                    QJsonObject payload=mergedResources;payload["maps"]=snapshot().value("maps");
                    QJsonObject mergeResponse{{"payload",payload},{"revision",rev},{"resourceSeq",double(seq)}};
                    resourceDirty=false;
                    if(apply(mergeResponse,false)){
                        {QScopedValueRollback<bool> guard(applyingRemote,true);ed.assetDatabase.fromJson(savedAssetDb);}
                        pendingRoomOperations=savedOps;assetDirty=savedAssetDirty;assetFullScanRequested=savedAssetFullScan;pendingAssetPaths=savedPaths;syncState.checkpointRequired=savedPendingHint;
                        resourceCursor=seq;remoteRevision=qMax(remoteRevision,rev);
                        auto updateBase=[&remoteResources](QJsonObject& base){const auto maps=base.value("maps").toArray();base=remoteResources;base["maps"]=maps;};
                        updateBase(serverBase);updateBase(localBase);
                        syncState.resourceConflict=false;resourceDirty=true;persistLink();
                        emit statusChanged(tr("Recursos globais mesclados — reenviando as diferenças"));scheduleResourceSync(0);
                        if(remoteRevision>revision)scheduleStructureRefresh(120);
                    } else {
                        resourceDirty=true;syncState.resourceConflict=true;deferredRemoteResources=remoteResources;deferredRemoteResourceSeq=seq;deferredRemoteResourceRevision=rev;
                        emit statusChanged(tr("Conflito em recursos globais — alterações locais preservadas"));
                    }
                } else {
                    resourceCursor=seq;syncState.resourceConflict=true;deferredRemoteResources=remoteResources;deferredRemoteResourceSeq=seq;deferredRemoteResourceRevision=rev;
                    emit statusChanged(tr("Conflito no mesmo Tileset/Autotile — alterações locais preservadas para revisão"));
                }
                return;
            }
            if(sending){
                resourceCursor=seq;remoteRevision=qMax(remoteRevision,rev);
                auto update=[this](QJsonObject& base,const QJsonObject& resources){const auto maps=base.value("maps").toArray();base=resources;base["maps"]=maps;};
                const QJsonObject sent=pendingResourceSnapshot;
                const QJsonObject remote=response.contains("resources")?response.value("resources").toObject():sent;
                // Keep the sent local base until the remote snapshot has been applied.
                // Otherwise a concurrent remote field could be sent back as a local deletion.
                update(serverBase,sent);update(localBase,sent);
                pendingResourceSnapshot={};pendingResourceOperationId.clear();
                resourceDirty=currentResources()!=sent;
                if(remote!=sent){
                    resourceCursor=qMax(qint64(0),seq-1);
                    if(resourceDirty||!pendingRoomOperations.isEmpty()){
                        deferredRemoteResources=remote;deferredRemoteResourceSeq=seq;deferredRemoteResourceRevision=rev;
                        // Reuse the three-way merge on the next exchange; its base is still sent.
                        scheduleResourceSync(120);
                    }else applyRemoteResources(remote,seq,rev);
                }else if(resourceDirty)scheduleResourceSync(120);
                else {persistLink();emit statusChanged(tr("Recursos sincronizados em tempo real"));}
                if(remoteRevision>revision)scheduleStructureRefresh(80);
            } else if(response.contains("resources")&&seq>resourceCursor){
                if(resourceDirty||syncState.checkpointRequired||!pendingRoomOperations.isEmpty()){
                    deferredRemoteResources=response.value("resources").toObject();deferredRemoteResourceSeq=seq;deferredRemoteResourceRevision=rev;
                    emit statusChanged(tr("Recursos novos aguardando suas alterações locais serem enviadas"));
                } else applyRemoteResources(response.value("resources").toObject(),seq,rev);
            } else resourceCursor=qMax(resourceCursor,seq);
        });
}

void CollaborationClient::applyRemoteResources(const QJsonObject& resources,qint64 sequence,int serverRevision) {
    if(resources.isEmpty())return;
    if(syncState.checkpointRequired||resourceDirty||!pendingRoomOperations.isEmpty()){
        deferredRemoteResources=resources;deferredRemoteResourceSeq=sequence;deferredRemoteResourceRevision=serverRevision;return;
    }
    const auto savedOps=pendingRoomOperations;const bool savedAssetDirty=assetDirty;const bool savedAssetFullScan=assetFullScanRequested;const auto savedPaths=pendingAssetPaths;const auto savedAssetDb=ed.assetDatabase.toJson();
    QJsonObject payload=resources;payload["maps"]=snapshot().value("maps");QJsonObject response{{"payload",payload},{"revision",serverRevision},{"resourceSeq",double(sequence)}};
    if(apply(response,false)){
        {QScopedValueRollback<bool> guard(applyingRemote,true);ed.assetDatabase.fromJson(savedAssetDb);}
        resourceCursor=sequence;remoteRevision=qMax(remoteRevision,serverRevision);
        pendingRoomOperations=savedOps;assetDirty=savedAssetDirty;assetFullScanRequested=savedAssetFullScan;pendingAssetPaths=savedPaths;resourceDirty=false;syncState.resourceConflict=false;persistLink();
        if(!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty())scheduleRoomFlush(0);scheduleAssetSync({},0);
        if(remoteRevision>revision)scheduleStructureRefresh(80);
        emit statusChanged(tr("Recursos globais atualizados"));
    }
}

QString CollaborationClient::teamProjectsRoot() const {
    QString documents=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if(documents.isEmpty())documents=QDir::homePath();
    const QString root=QDir(documents).filePath(QStringLiteral("LUDO/Projetos da Equipe"));
    QDir().mkpath(root);
    return root;
}

QString CollaborationClient::teamProjectPath(const QString& projectId,const QString& projectName) const {
    // A Home e o Modo de Equipe compartilham a mesma identidade lógica. Se o
    // projeto já foi integrado ao RPG Maker, reutilize essa cópia em vez de
    // criar uma segunda entrada em "Projetos da Equipe".
    const QString canonical=ProjectManagerDialog::projectPathForIdentity(projectId);
    if(!canonical.isEmpty())return canonical;
    const QString root=teamProjectsRoot();
    // O ID do projeto é a identidade real. Se o administrador renomear o
    // projeto no servidor, continuamos usando a mesma cópia local em vez de
    // criar outra pasta com o novo nome.
    QDir rootDir(root);
    const auto folders=rootDir.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    for(const auto& folder:folders){
        const QString existing=QDir(rootDir.filePath(folder)).filePath(QStringLiteral("Projeto.ludo"));
        QFile linkFile(existing+QStringLiteral(".team.json"));if(!linkFile.open(QIODevice::ReadOnly))continue;
        const auto link=QJsonDocument::fromJson(linkFile.readAll()).object();
        const QString linkedServerId=link.value(QStringLiteral("serverId")).toString();
        const bool sameServer=(!serverId.isEmpty()&&!linkedServerId.isEmpty())?linkedServerId==serverId:
            link.value(QStringLiteral("server")).toString()==server.toString();
        if(sameServer&&link.value(QStringLiteral("project")).toString()==projectId)return existing;
    }
    const QString safeName=safeTeamFolderName(projectName);
    auto candidate=[&](const QString& folder){return QDir(QDir(root).filePath(folder)).filePath(QStringLiteral("Projeto.ludo"));};
    const QString primary=candidate(safeName);
    const QFileInfo primaryInfo(primary);
    const QDir primaryDir(primaryInfo.absolutePath());
    if(!primaryDir.exists())return primary;

    QFile linkFile(primary+QStringLiteral(".team.json"));
    if(linkFile.open(QIODevice::ReadOnly)){
        const auto link=QJsonDocument::fromJson(linkFile.readAll()).object();
        const bool sameProject=link.value(QStringLiteral("project")).toString()==projectId;
        const QString linkedServerId=link.value(QStringLiteral("serverId")).toString();
        const bool sameServer=(!serverId.isEmpty()&&!linkedServerId.isEmpty())?linkedServerId==serverId:
            link.value(QStringLiteral("server")).toString()==server.toString();
        if(sameProject&&sameServer)return primary;
    }
    const auto entries=primaryDir.entryList(QDir::NoDotAndDotDot|QDir::AllEntries);
    if(entries.isEmpty())return primary;
    return candidate(QStringLiteral("%1-%2").arg(safeName,projectId.left(8)));
}

QJsonObject CollaborationClient::localProjectInfo(const QString& projectId,const QString& projectName,
                                                   const QJsonObject& remote) const {
    const QString path=teamProjectPath(projectId,projectName);
    QJsonObject result{{QStringLiteral("path"),path},{QStringLiteral("exists"),QFileInfo::exists(path)}};
    if(!QFileInfo::exists(path)){
        result[QStringLiteral("state")]=QStringLiteral("missing");
        return result;
    }
    QFile file(path+QStringLiteral(".team.json"));
    QJsonObject link;
    if(file.open(QIODevice::ReadOnly))link=QJsonDocument::fromJson(file.readAll()).object();
    const QString linkedServerId=link.value(QStringLiteral("serverId")).toString();
    const bool sameServer=(!serverId.isEmpty()&&!linkedServerId.isEmpty())?linkedServerId==serverId:
        link.value(QStringLiteral("server")).toString()==server.toString();
    const bool linked=sameServer&&link.value(QStringLiteral("project")).toString()==projectId;
    result[QStringLiteral("linked")]=linked;
    result[QStringLiteral("opened")]=QFileInfo(ed.projectPath).absoluteFilePath()==QFileInfo(path).absoluteFilePath();
    result[QStringLiteral("revision")]=link.value(QStringLiteral("revision")).toInt();
    if(!linked){result[QStringLiteral("state")]=QStringLiteral("local-unlinked");return result;}

    qint64 roomTotal=0;
    const auto cursors=link.value(QStringLiteral("roomCursors")).toObject();
    for(auto it=cursors.constBegin();it!=cursors.constEnd();++it)roomTotal+=qint64(it.value().toDouble());
    const qint64 localResource=qint64(link.value(QStringLiteral("resourceCursor")).toDouble());
    const qint64 localAsset=qint64(link.value(QStringLiteral("assetCursor")).toDouble());
    // O vínculo é atualizado em tempo real, enquanto o .ludo é salvo em pontos
    // seguros. Após um encerramento forçado, o sidecar pode estar mais novo
    // que o arquivo do projeto; nesse caso baixamos o estado consolidado para
    // nunca pular operações que só existiam na memória.
    const QFileInfo projectInfo(path),linkInfo(path+QStringLiteral(".team.json"));
    const bool checkpointCoversLink=!linkInfo.exists()||projectInfo.lastModified()>=linkInfo.lastModified();
    result[QStringLiteral("checkpointSafe")]=checkpointCoversLink;
    bool upToDate=checkpointCoversLink;
    if(!remote.isEmpty()){
        upToDate=upToDate&&link.value(QStringLiteral("revision")).toInt()==remote.value(QStringLiteral("revision")).toInt();
        if(remote.contains(QStringLiteral("roomSeqTotal")))upToDate=upToDate&&roomTotal==qint64(remote.value(QStringLiteral("roomSeqTotal")).toDouble());
        if(remote.contains(QStringLiteral("resourceSeq")))upToDate=upToDate&&localResource==qint64(remote.value(QStringLiteral("resourceSeq")).toDouble());
        if(remote.contains(QStringLiteral("assetSeq")))upToDate=upToDate&&localAsset==qint64(remote.value(QStringLiteral("assetSeq")).toDouble());
    }
    result[QStringLiteral("upToDate")]=upToDate;
    result[QStringLiteral("state")]=upToDate?QStringLiteral("ready"):QStringLiteral("update");
    return result;
}

bool CollaborationClient::loginToServer(const QUrl& requestedUrl,const QString& user,const QString& password) {
    if(!token.isEmpty())return server==requestedUrl;
    QUrl url=requestedUrl;
    if(!url.isValid()||(url.scheme()!=QStringLiteral("http")&&url.scheme()!=QStringLiteral("https"))||url.host().isEmpty()
       ||!url.userInfo().isEmpty()||url.hasQuery()||url.hasFragment()||(url.path()!=QString()&&url.path()!=QStringLiteral("/"))){
        QMessageBox::warning(window,tr("Endereço inválido"),tr("O endereço desta equipe não é válido."));return false;
    }
    if(url.scheme()==QStringLiteral("http")&&!privateOrLoopbackHost(url.host())){
        const auto choice=QMessageBox::warning(window,tr("Conexão sem criptografia"),
            tr("Esta equipe usa HTTP fora de uma rede privada reconhecida. Para uso pela Internet, prefira HTTPS. Continuar?"),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
        if(choice!=QMessageBox::Yes)return false;
    }
    server=url;QJsonObject response;
    if(!request(QStringLiteral("login"),{{QStringLiteral("name"),user.trimmed()},{QStringLiteral("password"),password}},response)){
        server=QUrl();return false;
    }
    const int apiProtocol=response.value(QStringLiteral("protocol")).toInt(1);
    serverProtocol=response.value(QStringLiteral("teamProtocol")).toInt(apiProtocol);
    token=response.value(QStringLiteral("token")).toString();
    if(apiProtocol!=1||serverProtocol!=9){
        QJsonObject ignored;if(!token.isEmpty())request(QStringLiteral("logout"),{},ignored,false,true);
        token.clear();role.clear();server=QUrl();serverProtocol=1;
        QMessageBox::warning(window,tr("Equipe desatualizada"),
            tr("Esta equipe usa uma versão incompatível do sistema colaborativo. Atualize ou reinicie o LUDO Team Server."));
        return false;
    }
    role=response.value(QStringLiteral("role")).toString();
    serverName=response.value(QStringLiteral("serverName")).toString(response.value(QStringLiteral("name")).toString(tr("Equipe LUDO")));
    serverId=response.value(QStringLiteral("serverId")).toString();
    QSettings settings;settings.setValue(QStringLiteral("collaboration/server"),server.toString());
    settings.setValue(QStringLiteral("collaboration/lastUser"),user.trimmed());
    timer.setInterval(3000);timer.start();
    setConnectionState(QStringLiteral("online"));
    emit statusChanged(tr("%1 · conectado como %2").arg(serverName,user.trimmed()));
    return true;
}

void CollaborationClient::offerResumeCurrentProject() {
    if(token.isEmpty()||ed.projectPath.isEmpty())return;
    QFile metadata(ed.projectPath+QStringLiteral(".team.json"));
    if(!metadata.open(QIODevice::ReadOnly))return;
    const auto link=QJsonDocument::fromJson(metadata.readAll()).object();
    const QString linkedServerId=link.value(QStringLiteral("serverId")).toString();
    const bool sameServer=(!serverId.isEmpty()&&!linkedServerId.isEmpty())?linkedServerId==serverId:
        link.value(QStringLiteral("server")).toString()==server.toString();
    if(!sameServer||link.value(QStringLiteral("identity")).toString()!=ed.projectId)return;
    if(QMessageBox::question(window,tr("Retomar projeto"),tr("Reconectar este projeto à equipe? Alterações offline serão preservadas."))!=QMessageBox::Yes)return;
    beginProgress(tr("Reconectando equipe"),tr("Verificando alterações feitas offline…"));updateProgress(20);
    project=link.value(QStringLiteral("project")).toString();identity=ed.projectId;localPath=ed.projectPath;
    revision=link.value(QStringLiteral("revision")).toInt();remoteRevision=revision;serverBase=link.value(QStringLiteral("serverBase")).toObject();localBase=link.value(QStringLiteral("localBase")).toObject();
    roomCursors.clear();const auto stored=link.value(QStringLiteral("roomCursors")).toObject();for(auto it=stored.constBegin();it!=stored.constEnd();++it)roomCursors[it.key()]=qint64(it.value().toDouble());
    resourceCursor=qint64(link.value(QStringLiteral("resourceCursor")).toDouble(0));
    assetCursor=qint64(link.value(QStringLiteral("assetCursor")).toDouble(0));pendingAssetCursor=assetCursor;remoteAssetSeq=assetCursor;
    assetStateInitialized=false;preserveLocalAssetsOnBootstrap=true;sharedAssets.clear();assetDirty=true;assetFullScanRequested=true;
    initializeRoomTracking();syncState.checkpointRequired=snapshot()!=localBase;
    updateProgress(100,syncState.checkpointRequired?tr("Alterações locais encontradas."):tr("Projeto pronto."));endProgress();heartbeat();
}

bool CollaborationClient::listRemoteProjects(QJsonArray& projects) {
    if(token.isEmpty())return false;
    QJsonObject list;if(!request(QStringLiteral("projects"),{},list,true,true))return false;
    projects=list.value(QStringLiteral("projects")).toArray();return true;
}

void CollaborationClient::listRemoteProjectsAsync(
    QObject* context,std::function<void(bool,const QJsonArray&,const QString&)> done) {
    if(!context){return;}
    if(token.isEmpty()){done(false,QJsonArray(),tr("Entre em uma equipe primeiro."));return;}
    QUrl url=server;url.setPath(QStringLiteral("/v1/projects"));
    QNetworkRequest req(url);req.setTransferTimeout(6000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    if(!token.isEmpty())req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    auto* reply=network.get(req);
    connect(reply,&QNetworkReply::finished,reply,&QObject::deleteLater);
    connect(reply,&QNetworkReply::finished,context,[this,reply,done=std::move(done)]() mutable {
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto document=QJsonDocument::fromJson(reply->readAll());
        const bool ok=reply->error()==QNetworkReply::NoError&&status==200&&document.isObject();
        const QString reason=document.object().value(QStringLiteral("error")).toString(reply->errorString());
        const QJsonArray projects=ok?document.object().value(QStringLiteral("projects")).toArray():QJsonArray();
        if(status==401)setConnectionState(QStringLiteral("offline"));
        else if(status>0)setConnectionState(QStringLiteral("online"));
        done(ok,projects,reason);
    });
}

bool CollaborationClient::openLocalTeamCopy(const QString& path,const QJsonObject& link,const QJsonObject& remote) {
    QString error;QScopedValueRollback<bool> guard(applyingRemote,true);
    if(!core::io::loadProject(ed,path,&error)){QMessageBox::warning(window,tr("Abrir projeto"),error);return false;}
    project=link.value(QStringLiteral("project")).toString();localPath=path;identity=ed.projectId;
    revision=link.value(QStringLiteral("revision")).toInt(remote.value(QStringLiteral("revision")).toInt());remoteRevision=revision;
    serverBase=link.value(QStringLiteral("serverBase")).toObject();localBase=link.value(QStringLiteral("localBase")).toObject();
    roomCursors.clear();const auto stored=link.value(QStringLiteral("roomCursors")).toObject();for(auto it=stored.constBegin();it!=stored.constEnd();++it)roomCursors[it.key()]=qint64(it.value().toDouble());
    resourceCursor=qint64(link.value(QStringLiteral("resourceCursor")).toDouble(0));
    assetCursor=qint64(link.value(QStringLiteral("assetCursor")).toDouble(0));pendingAssetCursor=assetCursor;remoteAssetSeq=assetCursor;
    syncState.checkpointRequired=snapshot()!=localBase;assetStateInitialized=false;preserveLocalAssetsOnBootstrap=true;assetDirty=true;assetFullScanRequested=true;
    initializeRoomTracking();persistLink();emit projectReloaded();emit ed.docsChanged();emit ed.layersChanged();emit ed.selectionChanged();emit ed.mapChanged();heartbeat();
    emit statusChanged(tr("%1 · tempo real ativo").arg(serverName));return true;
}

bool CollaborationClient::openRemoteProject(const QString& projectId,const QString& projectName,const QJsonObject& remote) {
    if(token.isEmpty())return false;
    if(attached()&&project==projectId&&QFileInfo(ed.projectPath).absoluteFilePath()==QFileInfo(teamProjectPath(projectId,projectName)).absoluteFilePath())return true;
    if(attached()&&hasPending()&&!synchronize())return false;
    if(ed.projectDirty&&!saveLocalCopy())return false;

    const auto local=localProjectInfo(projectId,projectName,remote);
    if(local.value(QStringLiteral("upToDate")).toBool(false)&&local.value(QStringLiteral("linked")).toBool(false)){
        QFile file(local.value(QStringLiteral("path")).toString()+QStringLiteral(".team.json"));QJsonObject link;
        if(file.open(QIODevice::ReadOnly))link=QJsonDocument::fromJson(file.readAll()).object();
        if(!link.isEmpty()){
            detachProject();
            if(openLocalTeamCopy(local.value(QStringLiteral("path")).toString(),link,remote))return true;
        }
    }

    beginProgress(tr("Abrindo projeto da equipe"),local.value(QStringLiteral("exists")).toBool()?tr("Atualizando sua cópia…"):tr("Baixando o projeto…"));
    QJsonObject response;if(!request(QStringLiteral("projects/")+projectId,{},response,true,false,tr("Recebendo o projeto…"),5,60)){endProgress();return false;}
    detachProject();project=projectId;updateProgress(65,tr("Preparando sua cópia local…"));
    if(!apply(response,true)){project.clear();endProgress();return false;}
    updateProgress(100,tr("Projeto pronto."));endProgress();return true;
}

void CollaborationClient::connectDialog() { connectToServerDialog(QString()); }
void CollaborationClient::connectToServerDialog(const QString& suggestedAddress) {
    if(!token.isEmpty()){QMessageBox::information(window,tr("Equipe"),tr("Você já está conectado a uma equipe."));return;}
    QDialog dialog(window);dialog.setWindowTitle(tr("Entrar com endereço"));QFormLayout layout(&dialog);
    const QString initial=suggestedAddress.trimmed().isEmpty()?QSettings().value(QStringLiteral("collaboration/server"),QStringLiteral("http://127.0.0.1:8787")).toString():suggestedAddress.trimmed();
    QLineEdit address(initial,&dialog),name(QSettings().value(QStringLiteral("collaboration/lastUser")).toString(),&dialog),password(&dialog);password.setEchoMode(QLineEdit::Password);
    layout.addRow(tr("Endereço"),&address);layout.addRow(tr("Usuário"),&name);layout.addRow(tr("Senha"),&password);
    QLabel hint(tr("Use esta opção somente quando a equipe não aparecer automaticamente no Hub."),&dialog);hint.setWordWrap(true);layout.addRow(&hint);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons.button(QDialogButtonBox::Ok)->setText(tr("Entrar"));layout.addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    const bool ok=loginToServer(QUrl(address.text().trimmed()),name.text(),password.text());password.clear();
    if(ok)offerResumeCurrentProject();
}
bool CollaborationClient::saveLocalCopy() {
    QString path=ed.projectPath;
    if(path.isEmpty() || QFileInfo(path).suffix().toLower()!="ludo") {
        path=QFileDialog::getSaveFileName(window,tr("Salvar cópia local"),"Projeto.ludo",tr("Projeto LUDO (*.ludo)"));
        if(path.isEmpty())return false;
        if(!path.endsWith(".ludo",Qt::CaseInsensitive))path+=".ludo";
    }
    // Sincronizar sem nenhuma edição local não deve reserializar o projeto
    // inteiro apenas para confirmar o que já está salvo em disco.
    if(!ed.projectDirty && QFileInfo::exists(path))return true;
    const bool linked=attached();QString error;
    if(!core::io::saveProject(ed,path,&error)){QMessageBox::warning(window,tr("Salvar"),error);return false;}
    if(linked)localPath=ed.projectPath;
    return true;
}
void CollaborationClient::publish() {
    if(token.isEmpty()){connectDialog();if(token.isEmpty())return;}
    if(role!="admin"){QMessageBox::information(window,tr("Equipe"),tr("Somente o administrador publica projetos."));return;}
    if(attached()){QMessageBox::information(window,tr("Equipe"),tr("Este projeto já está conectado. Use Enviar alterações."));return;}
    beginProgress(tr("Publicando projeto"),tr("Preparando a cópia local…"));
    updateProgress(8);
    if(!saveLocalCopy()){endProgress();return;}
    updateProgress(16,tr("Preparando Tilesets e Autotiles compartilháveis…"));
    QString assetError;
    if(!prepareTeamTilesetAssets(&assetError)){
        endProgress();QMessageBox::warning(window,tr("Recursos do projeto"),assetError);return;
    }
    updateProgress(20,tr("Preparando o projeto para envio…"));
    QJsonObject response;
    if(!request("projects",{{"payload",snapshot()}},response,false,false,tr("Enviando projeto para a equipe…"),25,65)){endProgress();return;}
    detachProject();project=response.value("id").toString();
    updateProgress(70,tr("Criando a cópia local vinculada…"));
    if(!apply(response,true)) {
        project.clear();
        endProgress();
        QMessageBox::information(window,tr("Projeto publicado"),tr("O projeto está no servidor. Abra o Hub da equipe para tentar preparar a cópia local novamente."));
        return;
    }
    updateProgress(100,tr("Projeto publicado."));
    endProgress();
    emit statusChanged(tr("Projeto publicado — sala colaborativa ativa"));
}
bool CollaborationClient::apply(const QJsonObject& response,bool chooseFolder) {
    const auto payload=response.value("payload").toObject();
    if(payload.isEmpty())return false;
    if(progress)updateProgress(qMax(progress->value(),72),tr("Preparando a atualização local…"));

    // Ao publicar o projeto atual, a cópia vinculada passa a viver em outra
    // pasta. Copiamos os Assets/ já catalogados para essa nova raiz antes de
    // carregar o payload limpo da equipe; assim o primeiro Asset Sync consegue
    // publicar os binários originais sem depender do caminho antigo da máquina.
    const bool seedLocalAssets=chooseFolder && !ed.projectId.isEmpty() &&
        ed.projectId==payload.value("projectId").toString();
    QString seedRoot;QJsonObject seedAssetDatabase;QVector<core::AssetRecord> seedRecords;
    if(seedLocalAssets){
        seedRoot=ed.projectRoot();QString assetError;
        if(!ed.assetDatabase.synchronize(seedRoot,&assetError)){
            QMessageBox::warning(window,tr("Recursos do projeto"),tr("Não foi possível preparar os recursos compartilhados: %1").arg(assetError));
            return false;
        }
        seedAssetDatabase=ed.assetDatabase.toJson();seedRecords=ed.assetDatabase.records();
    }

    QString path=localPath;
    if(chooseFolder) {
        path=teamProjectPath(project,payload.value(QStringLiteral("projectName")).toString());
        const QString directory=QFileInfo(path).absolutePath();
        if(!QDir().mkpath(directory)){
            QMessageBox::warning(window,tr("Projetos da equipe"),tr("Não foi possível preparar a pasta padrão dos projetos da equipe."));
            return false;
        }
        if(seedLocalAssets){
            for(const auto& record:seedRecords){
                if(record.missing)continue;const QString rel=portableAssetPath(record.path);if(rel.isEmpty())continue;
                const QString source=QDir(seedRoot).filePath(rel),target=QDir(directory).filePath(rel);
                QFile in(source);if(!in.open(QIODevice::ReadOnly))continue;
                QDir().mkpath(QFileInfo(target).absolutePath());QSaveFile out(target);const QByteArray bytes=in.readAll();
                if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit()){
                    QMessageBox::warning(window,tr("Recursos do projeto"),tr("Não foi possível copiar %1 para a cópia da equipe.").arg(rel));
                    return false;
                }
            }
        }
    }
    // Keep the previous local file as a separate, protected .ludo before replacing.
    if(QFileInfo::exists(path)) {
        const QString backup=path+"."+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")+".bak";
        if(!QFile::copy(path,backup)){QMessageBox::warning(window,tr("Cópia local"),tr("Não foi possível guardar a versão anterior."));return false;}
    }
    if(progress)updateProgress(qMax(progress->value(),77),tr("Validando os dados recebidos…"));
    const QString staging=path+".download";
    QSaveFile file(staging);
    const auto bytes=QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        QMessageBox::warning(window,tr("Cópia local"),tr("Não foi possível gravar o projeto recebido."));return false;
    }
    QString error;
    core::Editor preview;
    if(!core::io::loadProject(preview,staging,&error,core::io::ProjectLoadMode::ReadOnlyPreview)) {
        QMessageBox::warning(window,tr("Projeto inválido"),error);QFile::remove(staging);return false;
    }
    if(progress)updateProgress(qMax(progress->value(),84),tr("Atualizando o projeto…"));
    const auto oldDocs=ed.docs;
    const auto oldStamp=ed.session.customStamp;
    const auto oldSelection=ed.session.tsSel;
    const int oldTileset=ed.session.activeTilesetIdx;
    const QString oldAutotile=ed.session.activeAutotileId;
    const auto currentBeforeApply=chooseFolder?QJsonObject():snapshot();
    auto expected=chooseFolder?QJsonObject():outgoing(currentBeforeApply);
    const auto expectedMaps=mapsById(expected.value("maps").toArray());
    const auto receivedMaps=mapsById(payload.value("maps").toArray());
    expected.remove("maps");auto receivedResources=payload;receivedResources.remove("maps");
    const bool sameResources=!chooseFolder && expected==receivedResources;
    const QString active=ed.doc()?ed.doc()->id:QString();
    const QString rpgMakerRoot=chooseFolder?QString():ed.rpgMakerProjectRoot;
    QScopedValueRollback<bool> applyingGuard(applyingRemote,true);
    if(!core::io::loadProject(ed,staging,&error,core::io::ProjectLoadMode::ReadOnlyPreview)) {
        QMessageBox::warning(window,tr("Abrir projeto"),error);return false;
    }
    if(seedLocalAssets){
        ed.assetDatabase.fromJson(seedAssetDatabase);
        if(!ed.assetDatabase.synchronize(QFileInfo(path).absolutePath(),&error)){
            QMessageBox::warning(window,tr("Recursos do projeto"),tr("Não foi possível preparar o catálogo da cópia da equipe: %1").arg(error));
            return false;
        }
    }
    if(sameResources) {
        for(auto& doc:ed.docs) if(expectedMaps.value(doc.id)==receivedMaps.value(doc.id))
            for(const auto& old:oldDocs) if(old.id==doc.id){doc=old;break;}
        ed.session.customStamp=oldStamp;ed.session.tsSel=oldSelection;ed.session.activeTilesetIdx=oldTileset;ed.session.activeAutotileId=oldAutotile;
    }
    if(!chooseFolder) for(int i=0;i<ed.docs.size();++i)if(ed.docs[i].id==active){ed.activeDocIdx=i;break;}
    ed.projectPath=path;
    // Each member chooses their own RPG Maker target later; never inherit the host path.
    ed.rpgMakerProjectRoot=rpgMakerRoot;
    if(progress)updateProgress(qMax(progress->value(),92),tr("Salvando a cópia local…"));
    if(!core::io::saveProject(ed,path,&error)){QMessageBox::warning(window,tr("Salvar cópia"),error);return false;}
    QFile::remove(staging);
    localPath=path;identity=ed.projectId;revision=response.value("revision").toInt();remoteRevision=revision;
    serverBase=payload;localBase=snapshot();
    syncState.checkpointRequired=false;pendingRoomOperations=QJsonArray();pendingRoomResets.clear();roomRebaseMaps.clear();syncState.roomConflict=false;roomRequestPending=false;
    if(!assetStateInitialized)preserveLocalAssetsOnBootstrap=seedLocalAssets;
    initializeRoomTracking(response);
    if(progress)updateProgress(qMax(progress->value(),97),tr("Finalizando atualização…"));
    persistLink();
    emit projectReloaded();
    emit ed.layersChanged();emit ed.selectionChanged();emit ed.mapChanged();
    showStatus(response);return true;
}
bool CollaborationClient::applyIncremental(const QJsonObject& delta,const QJsonObject& authoritativePayload,
                                           const QJsonObject& response) {
    // Recursos globais podem afetar todos os mapas (tilesets, Wang, patterns,
    // prioridades etc.). Nessa situação ainda fazemos a recarga completa por
    // segurança. O caminho comum — mapas isolados — é aplicado diretamente.
    if(delta.contains("resources"))return false;

    QScopedValueRollback<bool> applyingGuard(applyingRemote,true);
    const QString activeId=ed.doc()?ed.doc()->id:QString();
    const QString activeLayerId=ed.doc()?ed.doc()->activeLayerId:QString();
    bool activeMapChanged=false;
    bool structureChanged=false;

    QSet<QString> deletedIds;
    for(const auto& value:delta.value("deletedMaps").toArray()) {
        const QString id=value.toString();
        if(!id.isEmpty())deletedIds.insert(id);
    }
    if(!deletedIds.isEmpty()) {
        for(int i=ed.docs.size()-1;i>=0;--i)if(deletedIds.contains(ed.docs[i].id)) {
            if(ed.docs[i].id==activeId)activeMapChanged=true;
            ed.docs.remove(i);structureChanged=true;
        }
    }

    QSet<QString> changedIds;
    for(const auto& value:delta.value("maps").toArray()) {
        const QJsonObject mapObject=value.toObject();
        const QString incomingId=mapObject.value("id").toString();
        if(incomingId.trimmed().isEmpty()) {
            QMessageBox::warning(window,tr("Sincronização"),tr("O servidor enviou um mapa sem identificador estável. A atualização incremental foi cancelada com segurança."));
            return false;
        }
        // Se o servidor devolveu exatamente a versão que já está aberta
        // localmente (caso comum após enviar o próprio mapa), não recriamos
        // camadas nem descartamos histórico/seleção daquele documento.
        if(const auto* existing=ed.mapById(incomingId);existing && teamMapPayload(*existing)==mapObject) {
            changedIds.insert(incomingId);
            continue;
        }
        core::MapDoc incoming;QString error;
        if(!core::io::loadProjectMapPayload(ed,mapObject,&incoming,&error)) {
            QMessageBox::warning(window,tr("Sincronização"),tr("Não foi possível aplicar o mapa recebido: %1").arg(error));
            return false;
        }
        changedIds.insert(incoming.id);
        const int idx=ed.mapIndexById(incoming.id);
        if(idx>=0) {
            const auto previous=ed.docs[idx];
            incoming.activeLayerIdx=previous.activeLayerIdx;
            incoming.activeLayerId=previous.activeLayerId;
            if(incoming.id==activeId)activeMapChanged=true;
            ed.docs[idx]=std::move(incoming);
        } else {
            ed.docs.push_back(std::move(incoming));
            structureChanged=true;
        }
    }

    if(delta.contains("mapOrder")) {
        const QJsonArray order=delta.value("mapOrder").toArray();
        QHash<QString,core::MapDoc> byId;
        for(auto& doc:ed.docs)byId.insert(doc.id,std::move(doc));
        QVector<core::MapDoc> reordered;reordered.reserve(order.size());
        for(const auto& value:order) {
            const QString id=value.toString();
            if(byId.contains(id))reordered.push_back(std::move(byId[id]));
        }
        if(reordered.size()!=ed.docs.size()) {
            QMessageBox::warning(window,tr("Sincronização"),tr("A ordem de mapas recebida não corresponde à cópia local. A cópia local foi preservada."));
            return false;
        }
        ed.docs=std::move(reordered);structureChanged=true;
    }

    if(ed.docs.isEmpty()) {
        QMessageBox::warning(window,tr("Sincronização"),tr("A atualização removeria todos os mapas do projeto. A cópia local foi preservada."));
        return false;
    }

    int activeIndex=ed.mapIndexById(activeId);
    if(activeIndex<0)activeIndex=qBound(0,ed.activeDocIdx,int(ed.docs.size())-1);
    ed.activeDocIdx=activeIndex;
    if(activeMapChanged && !activeLayerId.isEmpty()) {
        ed.docs[activeIndex].activeLayerId=activeLayerId;
        const auto layer=ed.activeLayer();
        if(layer)ed.session.selectedLayerId=layer->id;
        else if(!ed.docs[activeIndex].layers.isEmpty())ed.setActiveLayerIdx(0);
    }

    // Persistimos a cópia local sem reabrir o projeto. Ainda é um save
    // atômico completo, mas todo o custo de parse/reload dos mapas não
    // alterados desaparece do caminho comum.
    updateProgress(88,tr("Salvando a cópia local atualizada…"));
    QString saveError;
    ed.projectDirty=true;
    if(!core::io::saveProject(ed,localPath,&saveError)) {
        QMessageBox::warning(window,tr("Sincronização"),saveError);
        return false;
    }

    // O servidor guarda a forma autoritativa; a base local guarda a forma
    // normalizada pelo editor somente para os mapas que acabaram de mudar.
    serverBase=authoritativePayload;
    auto normalizedMaps=mapsById(localBase.value("maps").toArray());
    for(const QString& id:deletedIds)normalizedMaps.remove(id);
    for(const QString& id:changedIds) {
        const auto* doc=ed.mapById(id);
        if(doc)normalizedMaps[id]=teamMapPayload(*doc);
    }
    QJsonArray normalizedOrder;
    for(const auto& value:authoritativePayload.value("maps").toArray()) {
        const QString id=value.toObject().value("id").toString();
        if(!normalizedMaps.contains(id)) {
            const auto* doc=ed.mapById(id);
            if(doc)normalizedMaps[id]=teamMapPayload(*doc);
        }
        if(normalizedMaps.contains(id))normalizedOrder.append(normalizedMaps.value(id));
    }
    QJsonObject normalizedBase=localBase;
    normalizedBase["maps"]=normalizedOrder;
    localBase=normalizedBase;
    revision=response.value("revision").toInt(revision);remoteRevision=revision;
    syncState.checkpointRequired=false;
    initializeRoomTracking();
    updateProgress(95,tr("Atualizando o vínculo da equipe…"));
    persistLink();

    // Atualização seletiva da UI: nada de refresh global do workspace quando
    // só um mapa remoto mudou.
    if(structureChanged||!changedIds.isEmpty())emit ed.docsChanged();
    if(activeMapChanged) {
        emit ed.layersChanged();
        emit ed.selectionChanged();
        emit ed.mapChanged();
        emit ed.historyChanged();
    }
    showStatus(response);
    return true;
}
void CollaborationClient::openRemote() {
    if(token.isEmpty()){connectDialog();if(token.isEmpty())return;}
    QJsonArray projects;if(!listRemoteProjects(projects))return;
    QStringList labels;for(const auto& value:projects){const auto item=value.toObject();labels<<item.value(QStringLiteral("name")).toString(tr("Projeto"));}
    if(labels.isEmpty()){QMessageBox::information(window,tr("Projetos da equipe"),tr("Nenhum projeto publicado nesta equipe."));return;}
    bool ok=false;const QString choice=QInputDialog::getItem(window,tr("Projetos da equipe"),tr("Projeto"),labels,0,false,&ok);if(!ok)return;
    const int index=labels.indexOf(choice);if(index<0)return;const auto remote=projects.at(index).toObject();
    openRemoteProject(remote.value(QStringLiteral("id")).toString(),remote.value(QStringLiteral("name")).toString(),remote);
}
void CollaborationClient::persistLink() {
    QSaveFile file(localPath+".team.json");
    QJsonObject cursorObject;for(auto it=roomCursors.constBegin();it!=roomCursors.constEnd();++it)cursorObject[it.key()]=double(it.value());
    QJsonObject link{{"server",server.toString()},{"serverId",serverId},{"protocol",serverProtocol},
        {"project",project},{"identity",identity},{"revision",revision},{"roomCursors",cursorObject},
        {"resourceCursor",double(resourceCursor)},{"assetCursor",double(assetCursor)},
        {"serverBase",serverBase},{"localBase",localBase}};
    const auto bytes=QJsonDocument(link).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
        QMessageBox::warning(window,tr("Equipe"),tr("Não foi possível guardar o vínculo com o servidor. A cópia local foi mantida, mas a retomada após fechar o editor não estará disponível."));
}
void CollaborationClient::initializeRoomTracking(const QJsonObject& response) {
    observedHistoryPtr.clear();observedHistoryRevision.clear();
    for(const auto& doc:ed.docs){observedHistoryPtr.insert(doc.id,doc.historyPtr);observedHistoryRevision.insert(doc.id,doc.historyRevision);}
    if(response.contains("roomSeqs")) {
        roomCursors.clear();
        const auto seqs=response.value("roomSeqs").toObject();
        for(auto it=seqs.constBegin();it!=seqs.constEnd();++it)roomCursors.insert(it.key(),qint64(it.value().toDouble()));
    }
    {
        // Recursos globais chegam junto do payload do projeto, portanto este
        // cursor pode avançar imediatamente. Assets binários precisam primeiro
        // buscar/reconstruir o manifesto; nunca pulamos direto para assetSeq.
        if(response.contains("resourceSeq"))resourceCursor=qint64(response.value("resourceSeq").toDouble(resourceCursor));
        if(response.contains("assetSeq"))remoteAssetSeq=qMax(remoteAssetSeq,qint64(response.value("assetSeq").toDouble(assetCursor)));
        if(!assetStateInitialized){assetCursor=0;pendingAssetCursor=0;sharedAssets.clear();assetDirty=true;}
        scheduleAssetSync({},0);
        scheduleResourceSync(0);
    }
    timer.setInterval(3000);
    QTimer::singleShot(0,this,&CollaborationClient::ensureRoomWatch);
    if(!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty())scheduleRoomFlush();
}
void CollaborationClient::queueRoomOperation(QJsonObject operation) {
    if(!attached()||applyingRemote)return;
    const QString mapId=operation.value("map").toString();
    const QString kind=operation.value("kind").toString();
    const QString layerId=operation.value("layer").toString();
    if(mapId.isEmpty()||kind.isEmpty())return;

    if(kind=="map.create"){
        QJsonArray rebuilt;
        for(const auto& value:pendingRoomOperations)if(value.toObject().value("map").toString()!=mapId)rebuilt.append(value);
        rebuilt.append(operation);pendingRoomOperations=rebuilt;scheduleRoomFlush();return;
    }
    if(kind=="map.delete"){
        // Excluir um mapa invalida qualquer diff ainda não confirmado daquele
        // mapa. Se ele acabou de nascer localmente e map.create ainda não foi
        // enviado, as duas operações se anulam e nada vai para a rede.
        bool unsentCreate=false;QJsonArray rebuilt;
        for(const auto& value:pendingRoomOperations){
            const auto old=value.toObject();
            if(old.value("map").toString()!=mapId){rebuilt.append(old);continue;}
            if(old.value("kind").toString()=="map.create"&&old.value("id").toString().isEmpty())unsentCreate=true;
        }
        pendingRoomOperations=rebuilt;
        if(!unsentCreate)pendingRoomOperations.append(operation);
        scheduleRoomFlush();return;
    }
    if(kind=="map.meta"){
        // Metadado estrutural é estado-final. Se a mesma versão já está em
        // voo, não criamos uma segunda operação idêntica; se a versão em voo
        // ficou antiga, mantemos apenas UM follow-up ainda não enviado.
        for(int i=pendingRoomOperations.size()-1;i>=0;--i){
            auto old=pendingRoomOperations.at(i).toObject();
            if(old.value("kind").toString()!=kind||old.value("map").toString()!=mapId)continue;
            if(old.value("value")==operation.value("value"))return;
            if(old.value("id").toString().isEmpty()){
                old["value"]=operation.value("value");pendingRoomOperations[i]=old;scheduleRoomFlush();return;
            }
            break;
        }
    }
    if(kind=="map.order"){
        // A ordem também é estado-final. Preservamos a operação em voo, mas
        // uma ordem idêntica não precisa ser reenviada e ordens ainda não
        // enviadas são substituídas pela mais recente.
        for(int i=pendingRoomOperations.size()-1;i>=0;--i){
            const auto old=pendingRoomOperations.at(i).toObject();
            if(old.value("kind").toString()!=kind)continue;
            if(old.value("order")==operation.value("order"))return;
            if(!old.value("id").toString().isEmpty())break;
        }
        QJsonArray rebuilt;
        for(const auto& value:pendingRoomOperations){const auto old=value.toObject();if(old.value("kind").toString()==kind&&old.value("id").toString().isEmpty())continue;rebuilt.append(old);}
        rebuilt.append(operation);pendingRoomOperations=rebuilt;scheduleRoomFlush();return;
    }
    if(kind=="map.replace"){
        // Evita produzir vários snapshots iguais enquanto o anterior aguarda
        // ACK. Se um snapshot em voo ficou antigo, o bloco genérico abaixo
        // conserva-o e substitui somente o follow-up ainda não enviado.
        for(int i=pendingRoomOperations.size()-1;i>=0;--i){
            const auto old=pendingRoomOperations.at(i).toObject();
            if(old.value("map").toString()!=mapId||old.value("kind").toString()!=kind)continue;
            if(old.value("value")==operation.value("value"))return;
            if(!old.value("id").toString().isEmpty())break;
        }
    }

    // Substituições maiores já contêm o estado final e tornam diffs anteriores
    // do mesmo escopo redundantes. Só coalescemos operações ainda não enviadas.
    QJsonArray rebuilt;
    for(const auto& value:pendingRoomOperations) {
        const auto old=value.toObject();
        bool superseded=false;
        if(old.value("id").toString().isEmpty() && old.value("map").toString()==mapId) {
            if(kind=="map.replace")superseded=true;
            else if(kind=="layer.replace" && old.value("layer").toString()==layerId) {
                const QString oldKind=old.value("kind").toString();
                superseded=oldKind=="layer.replace"||oldKind=="tile.cells"||oldKind=="objects.patch";
            }
        }
        if(!superseded)rebuilt.append(old);
    }
    pendingRoomOperations=rebuilt;

    // Pinceladas consecutivas do mesmo layer são reduzidas por coordenada.
    // O canal é orientado a eventos; o debounce curto só agrupa rajadas do
    // mesmo gesto para evitar uma requisição por célula.
    if((kind=="tile.cells"||kind=="region.cells")&&!operation.value("changes").toArray().isEmpty()) {
        for(int i=pendingRoomOperations.size()-1;i>=0;--i) {
            auto old=pendingRoomOperations.at(i).toObject();
            if(!old.value("id").toString().isEmpty())continue;
            if(old.value("kind").toString()!=kind||old.value("map").toString()!=mapId||old.value("layer").toString()!=layerId)continue;
            QHash<QString,QJsonObject> cells;
            for(const auto& item:old.value("changes").toArray()) {auto c=item.toObject();cells[QString::number(c.value("x").toInt())+","+QString::number(c.value("y").toInt())]=c;}
            for(const auto& item:operation.value("changes").toArray()) {auto c=item.toObject();cells[QString::number(c.value("x").toInt())+","+QString::number(c.value("y").toInt())]=c;}
            QJsonArray merged;for(auto it=cells.constBegin();it!=cells.constEnd();++it)merged.append(it.value());old["changes"]=merged;pendingRoomOperations[i]=old;scheduleRoomFlush();return;
        }
    }
    if(kind=="objects.patch") {
        for(int i=pendingRoomOperations.size()-1;i>=0;--i) {
            auto old=pendingRoomOperations.at(i).toObject();
            if(!old.value("id").toString().isEmpty())continue;
            if(old.value("kind").toString()!=kind||old.value("map").toString()!=mapId||old.value("layer").toString()!=layerId)continue;
            QHash<QString,QJsonObject> objects;QSet<QString> deleted;
            for(const auto& item:old.value("upsert").toArray()){const auto object=item.toObject();objects[object.value("id").toString()]=object;}
            for(const auto& item:old.value("delete").toArray()){const QString id=item.toString();objects.remove(id);deleted.insert(id);}
            for(const auto& item:operation.value("upsert").toArray()){const auto object=item.toObject();const QString id=object.value("id").toString();if(!id.isEmpty()){deleted.remove(id);objects[id]=object;}}
            for(const auto& item:operation.value("delete").toArray()){const QString id=item.toString();objects.remove(id);deleted.insert(id);}
            QJsonArray upsert,removed;for(auto it=objects.constBegin();it!=objects.constEnd();++it)upsert.append(it.value());for(const auto& id:deleted)removed.append(id);
            old["upsert"]=upsert;old["delete"]=removed;pendingRoomOperations[i]=old;scheduleRoomFlush();return;
        }
    }
    pendingRoomOperations.append(operation);
    scheduleRoomFlush();
}
void CollaborationClient::scheduleRoomFlush(int delayMs) {
    if(!attached()||(pendingRoomOperations.isEmpty()&&pendingRoomResets.isEmpty()))return;
    const int delay=qBound(0,delayMs,1000);
    if(roomFlushTimer.isActive()&&roomFlushTimer.remainingTime()<=delay)return;
    roomFlushTimer.start(delay);
}
void CollaborationClient::captureHistoryChange() {
    if(applyingRemote||!attached())return;
    auto* doc=ed.doc();if(!doc)return;
    const int now=doc->historyPtr;const quint64 historyRevision=doc->historyRevision;
    if(!observedHistoryPtr.contains(doc->id)){observedHistoryPtr[doc->id]=now;observedHistoryRevision[doc->id]=historyRevision;return;}
    const int before=observedHistoryPtr.value(doc->id);const quint64 beforeRevision=observedHistoryRevision.value(doc->id,historyRevision);
    // switchDoc also emits historyChanged, but does not create a transaction.
    if(historyRevision==beforeRevision){observedHistoryPtr[doc->id]=now;return;}
    if(role=="viewer") {observedHistoryPtr[doc->id]=now;observedHistoryRevision[doc->id]=historyRevision;markTeamPending();return;}
    if(!mapKnownToServer(doc->id)){
        // Enquanto o map.create ainda não foi confirmado, as edições apenas
        // atualizam o snapshot daquela criação. Nunca enviamos tile.cells para
        // uma sala que o servidor ainda não conhece.
        scheduleMapStructureReconcile(0);
        bool createInFlight=false;
        for(int i=0;i<pendingRoomOperations.size();++i){
            auto op=pendingRoomOperations.at(i).toObject();
            if(op.value("kind").toString()!="map.create"||op.value("map").toString()!=doc->id)continue;
            if(op.value("id").toString().isEmpty()){op["value"]=teamMapPayload(*doc);pendingRoomOperations[i]=op;}
            else createInFlight=true;
            break;
        }
        if(createInFlight)queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","map.replace"},{"value",teamMapPayload(*doc)}});
        observedHistoryPtr[doc->id]=now;observedHistoryRevision[doc->id]=historyRevision;scheduleRoomFlush();return;
    }

    auto queueEntry=[this,doc](const core::HistoryEntry& entry,bool forward){
        if(entry.tilesetSnapshot){
            ++atlasPreparationRevision;atlasPreparationReady=false;syncState.resourceConflict=false;resourceDirty=true;scheduleResourceSync();
            return;
        }
        if(entry.regionDiff) {
            QJsonArray changes;
            for(const auto& change:entry.regionChanges)changes.append(QJsonObject{{"x",change.x},{"y",change.y},{"value",forward?change.after:change.before}});
            queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","region.cells"},{"changes",changes}});return;
        }
        if(entry.document) {
            const auto before=snapshotMap(forward?entry.beforeDoc:entry.afterDoc,doc->id);
            const auto after=snapshotMap(forward?entry.afterDoc:entry.beforeDoc,doc->id);
            const auto properties=mapPropertyChanges(before,after);
            if(!properties.isEmpty())queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","map.properties"},{"value",properties}});
            else queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","map.replace"},{"baseSeq",double(roomCursors.value(doc->id,0))},{"value",teamMapPayload(*doc)}});
            return;
        }
        const auto layer=teamLayerPtr(doc->layers,entry.layerId);
        if(!layer) {
            queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","map.replace"},{"baseSeq",double(roomCursors.value(doc->id,0))},{"value",teamMapPayload(*doc)}});return;
        }
        if(entry.tileDiff && layer->type==core::LayerType::Tile) {
            QJsonArray changes;
            for(const auto& change:entry.tileChanges)changes.append(QJsonObject{{"x",change.x},{"y",change.y},{"value",core::io::cellToJson(forward?change.after:change.before)}});
            queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","tile.cells"},{"layer",entry.layerId},{"changes",changes}});return;
        }
        if(layer->type==core::LayerType::Object && objectLayerMetadataSame(entry.beforeLayer,entry.afterLayer)) {
            const auto& source=forward?entry.beforeLayer.objects:entry.afterLayer.objects;
            const auto& target=forward?entry.afterLayer.objects:entry.beforeLayer.objects;
            QStringList sourceOrder,targetOrder;QHash<QString,QJsonObject> sourceById,targetById;
            for(const auto& object:source){sourceOrder<<object.id;sourceById[object.id]=teamObjectJson(object);}
            for(const auto& object:target){targetOrder<<object.id;targetById[object.id]=teamObjectJson(object);}
            // Reordenação de objetos é estrutural dentro da camada: nesse caso
            // substituímos só a camada, não o mapa inteiro.
            QSet<QString> sourceIds,targetIds;
            for(const auto& id:sourceOrder)sourceIds.insert(id);
            for(const auto& id:targetOrder)targetIds.insert(id);
            if(sourceOrder!=targetOrder && sourceIds==targetIds) {
                const auto map=teamMapPayload(*doc);const auto payload=teamLayerJson(map.value("layers").toArray(),entry.layerId);
                if(!payload.isEmpty())queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","layer.replace"},{"layer",entry.layerId},{"value",payload}});
                return;
            }
            QJsonArray upsert,deleted;
            for(auto it=targetById.constBegin();it!=targetById.constEnd();++it)if(!sourceById.contains(it.key())||sourceById.value(it.key())!=it.value())upsert.append(it.value());
            for(auto it=sourceById.constBegin();it!=sourceById.constEnd();++it)if(!targetById.contains(it.key()))deleted.append(it.key());
            if(!upsert.isEmpty()||!deleted.isEmpty())queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","objects.patch"},{"layer",entry.layerId},{"upsert",upsert},{"delete",deleted}});
            return;
        }
        const auto map=teamMapPayload(*doc);const auto payload=teamLayerJson(map.value("layers").toArray(),entry.layerId);
        if(!payload.isEmpty())queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","layer.replace"},{"layer",entry.layerId},{"value",payload}});
        else queueRoomOperation(QJsonObject{{"map",doc->id},{"kind","map.replace"},{"baseSeq",double(roomCursors.value(doc->id,0))},{"value",map}});
    };

    if(now>before)for(int i=before+1;i<=now&&i<doc->history.size();++i)queueEntry(doc->history[i],true);
    else if(now<before)for(int i=before;i>now&&i>=0&&i<doc->history.size();--i)queueEntry(doc->history[i],false);
    else if(now>=0&&now<doc->history.size())
        // At the history cap, appending a new transaction prunes the oldest
        // entry and historyPtr stays unchanged. historyRevision distinguishes
        // that real edit from a mere UI/doc switch.
        queueEntry(doc->history[now],true);
    observedHistoryPtr[doc->id]=now;observedHistoryRevision[doc->id]=historyRevision;
}
void CollaborationClient::updateTeamBaseForMap(const QString& mapId) {
    const auto* doc=ed.mapById(mapId);if(!doc)return;
    for(const auto& value:pendingRoomOperations)if(value.toObject().value("map").toString()==mapId)return;
    const auto payload=teamMapPayload(*doc);
    auto patch=[&](QJsonObject& base){auto maps=base.value("maps").toArray();for(int i=0;i<maps.size();++i)if(maps.at(i).toObject().value("id").toString()==mapId){maps[i]=payload;base["maps"]=maps;return;}maps.append(payload);base["maps"]=maps;};
    patch(serverBase);patch(localBase);
}
bool CollaborationClient::applyRoomOperation(const QJsonObject& envelope) {
    const auto operation=envelope.value("operation").toObject();
    const QString mapId=operation.value("map").toString();
    const QString kind=operation.value("kind").toString();
    if(kind=="map.create"){
        if(ed.mapById(mapId))return true;
        core::MapDoc incoming;QString error;if(!core::io::loadProjectMapPayload(ed,operation.value("value").toObject(),&incoming,&error))return false;
        if(incoming.id!=mapId)return false;
        ed.docs.push_back(std::move(incoming));return true;
    }
    if(kind=="map.delete"){
        const int index=ed.mapIndexById(mapId);
        if(index<0)return true; // delete idempotente
        if(ed.docs.size()<=1)return false;
        const QString activeId=ed.doc()?ed.doc()->id:QString();
        ed.docs.remove(index);
        if(activeId==mapId)ed.activeDocIdx=qBound(0,index,int(ed.docs.size())-1);
        else {
            const int restored=ed.mapIndexById(activeId);
            ed.activeDocIdx=restored>=0?restored:qBound(0,ed.activeDocIdx,int(ed.docs.size())-1);
        }
        ed.session.selectedObjectId.clear();ed.session.selectedObjectIds.clear();
        ed.session.selectedLayerId=ed.activeLayer()?ed.activeLayer()->id:QString();
        roomCursors.remove(mapId);pendingRoomResets.remove(mapId);
        return true;
    }
    if(kind=="map.order"){
        const auto order=operation.value("order").toArray();
        if(order.size()!=ed.docs.size())return false;
        // Valide a ordem inteira ANTES de mover qualquer MapDoc. Um pacote
        // inválido não pode deixar ed.docs parcialmente moved-from.
        QSet<QString> existing;for(const auto& item:ed.docs)existing.insert(item.id);
        QSet<QString> seen;for(const auto& value:order){const QString id=value.toString();if(id.isEmpty()||seen.contains(id)||!existing.contains(id))return false;seen.insert(id);}
        if(seen!=existing)return false;
        const QString activeId=ed.doc()?ed.doc()->id:QString();
        QHash<QString,core::MapDoc> byId;for(auto& item:ed.docs)byId.insert(item.id,std::move(item));
        QVector<core::MapDoc> reordered;reordered.reserve(order.size());
        for(const auto& value:order)reordered.push_back(std::move(byId[value.toString()]));
        ed.docs=std::move(reordered);const int restored=ed.mapIndexById(activeId);if(restored>=0)ed.activeDocIdx=restored;
        return true;
    }

    auto* doc=ed.mapById(mapId);if(!doc)return false;
    if(kind=="map.meta"){
        const auto value=operation.value("value").toObject();
        const QString name=value.value("name").toString().trimmed();
        const QString parentId=value.value("parentId").toString();
        const QString variationBaseId=value.value("variationBaseId").toString();
        const QString variationName=value.value("variationName").toString();
        const int rpgMakerMapId=value.value("rpgMakerMapId").toInt();
        if(name.isEmpty()||rpgMakerMapId<0||parentId==mapId||variationBaseId==mapId)return false;
        if(!parentId.isEmpty()&&!ed.mapById(parentId))return false;
        if(!variationBaseId.isEmpty()&&!ed.mapById(variationBaseId))return false;
        doc->name=name;doc->parentId=parentId;doc->variationBaseId=variationBaseId;doc->variationName=variationName;
        doc->rpgMakerMapId=rpgMakerMapId;doc->rpgMakerImported=value.value("rpgMakerImported").toBool(false);
        return true;
    }
    if(kind=="tile.cells") {
        const auto layer=teamLayerPtr(doc->layers,operation.value("layer").toString());if(!layer||layer->type!=core::LayerType::Tile)return false;
        const auto changes=operation.value("changes").toArray();
        // Validação em duas fases: nenhuma célula é aplicada até sabermos que
        // TODA a operação cabe no estado local. Evita cursor avançar após uma
        // aplicação parcial.
        for(const auto& value:changes){if(!value.isObject())return false;const auto c=value.toObject();if(!c.value("x").isDouble()||!c.value("y").isDouble())return false;const int x=c.value("x").toInt(-1),y=c.value("y").toInt(-1);if(!layer->inBounds(x,y))return false;}
        for(const auto& value:changes){const auto c=value.toObject();const int x=c.value("x").toInt(),y=c.value("y").toInt();layer->data2D[y][x]=core::io::cellFromJson(c.value("value"));}
        return true;
    }
    if(kind=="region.cells") {
        const auto changes=operation.value("changes").toArray();
        for(const auto& value:changes){if(!value.isObject())return false;const auto c=value.toObject();if(!c.value("x").isDouble()||!c.value("y").isDouble()||!c.value("value").isDouble())return false;const int x=c.value("x").toInt(-1),y=c.value("y").toInt(-1),region=c.value("value").toInt(-1);if(!doc->regionInBounds(x,y)||region<0||region>255)return false;}
        for(const auto& value:changes){const auto c=value.toObject();doc->setRegionIdAt(c.value("x").toInt(),c.value("y").toInt(),c.value("value").toInt());}
        return true;
    }
    if(kind=="objects.patch") {
        const auto layer=teamLayerPtr(doc->layers,operation.value("layer").toString());if(!layer||layer->type!=core::LayerType::Object)return false;
        QSet<QString> deleted;for(const auto& value:operation.value("delete").toArray()){if(!value.isString()||value.toString().isEmpty())return false;deleted.insert(value.toString());}
        QVector<core::MapObject> upsert;for(const auto& value:operation.value("upsert").toArray()){if(!value.isObject()||value.toObject().value("id").toString().isEmpty())return false;upsert.push_back(teamObjectFromJson(value.toObject()));}
        for(int i=layer->objects.size()-1;i>=0;--i)if(deleted.contains(layer->objects[i].id))layer->objects.remove(i);
        for(const auto& incoming:upsert){bool found=false;for(auto& object:layer->objects)if(object.id==incoming.id){object=incoming;found=true;break;}if(!found)layer->objects.push_back(incoming);}
        return true;
    }
    QJsonObject mapPayload=teamMapPayload(*doc);
    if(kind=="layer.replace") {
        auto layers=mapPayload.value("layers").toArray();if(!replaceTeamLayerJson(layers,operation.value("layer").toString(),operation.value("value").toObject()))return false;mapPayload["layers"]=layers;
    } else if(kind=="map.properties") {
        const auto changes=operation.value("value").toObject();
        if(changes.isEmpty())return false;
        for(auto it=changes.begin();it!=changes.end();++it){
            if((it.key()!="map"&&it.key()!="reflectionSettings")||!it.value().isObject())return false;
            if(it.key()=="map"){
                const auto patch=it.value().toObject();auto props=mapPayload.value("map").toObject();
                for(const auto& key:{"width","height","tileWidth","tileHeight"})if(patch.contains(key)&&patch.value(key)!=props.value(key))return false;
                for(auto field=patch.begin();field!=patch.end();++field){if(field.value().isNull())props.remove(field.key());else props[field.key()]=field.value();}
                mapPayload["map"]=props;
            }else mapPayload[it.key()]=it.value();
        }
    } else if(kind=="map.replace")mapPayload=operation.value("value").toObject();
    else return false;
    core::MapDoc incoming;QString error;if(!core::io::loadProjectMapPayload(ed,mapPayload,&incoming,&error)||incoming.id!=mapId)return false;
    const QString activeLayer=doc->activeLayerId;const int index=ed.mapIndexById(mapId);if(index<0)return false;
    if(kind=="map.properties"){doc->map=incoming.map;doc->reflectionSettings=incoming.reflectionSettings;return true;}
    incoming.activeLayerId=activeLayer;incoming.activeLayerIdx=doc->activeLayerIdx;ed.docs[index]=std::move(incoming);return true;
}

bool CollaborationClient::applyRoomResponse(const QJsonObject& response) {
    if(TeamRasterTransport::hasReferences(response)){
        if(rasterDownloading)return false;
        rasterDownloading=true;const quint64 requestEpoch=teamEpoch;const QString requestedProject=project,requestedToken=token;
        QUrl base=server;base.setPath("/v1/projects/"+project+"/asset-blobs");
        rasterTransport->download(response,base,token,[this,requestEpoch,requestedProject,requestedToken](bool ok,const QJsonValue& expanded){
            if(teamEpoch!=requestEpoch||project!=requestedProject||token!=requestedToken)return;
            rasterDownloading=false;
            if(ok&&!busy)applyRoomResponse(expanded.toObject());
            else if(!ok)emit statusChanged(tr("Aguardando imagem compartilhada — cursor preservado"));
            scheduleRoomFlush(ok?0:900);roomWatchRetryTimer.start(ok?0:900);
        });
        return false;
    }
    if(response.contains("presence"))lastPresence=response.value("presence").toArray();
    remoteRevision=qMax(remoteRevision,response.value("revision").toInt(revision));
    remoteAssetSeq=qMax(remoteAssetSeq,qint64(response.value("assetSeq").toDouble(assetCursor)));
    const qint64 remoteResourceSeq=qint64(response.value("resourceSeq").toDouble(resourceCursor));
    if(remoteAssetSeq>assetCursor)scheduleAssetSync({},0);
    if(remoteResourceSeq>resourceCursor)scheduleResourceSync(0);

    QSet<QString> acked,ackMaps,ackedCreates;QVector<QJsonObject> ackedOperations;
    for(const auto& value:response.value("acked").toArray())acked.insert(value.toString());
    if(!acked.isEmpty()){
        QJsonArray keep;
        for(const auto& value:pendingRoomOperations){
            const auto op=value.toObject();
            if(acked.contains(op.value("id").toString())){
                ackedOperations.push_back(op);const QString mapId=op.value("map").toString();
                if(op.value("kind").toString()=="map.create")ackedCreates.insert(mapId);else ackMaps.insert(mapId);
            } else keep.append(op);
        }
        pendingRoomOperations=keep;
        syncState.roomConflict=false;
    }

    QSet<QString> touched,failedMaps,successfulResets;
    bool activeChanged=false,activeLayersChanged=false,activeSelectionChanged=false,docsChangedNeeded=false;
    bool remoteStructureChanged=false;
    const auto seqs=response.value("roomSeqs").toObject();
    QScopedValueRollback<bool> applyingGuard(applyingRemote,true);

    auto removeBaseMap=[&](QJsonObject& base,const QString& mapId){QJsonArray maps;for(const auto& value:base.value("maps").toArray())if(value.toObject().value("id").toString()!=mapId)maps.append(value);base["maps"]=maps;};
    auto upsertBaseMap=[&](QJsonObject& base,const QJsonObject& map){const QString mapId=map.value("id").toString();if(mapId.isEmpty())return;QJsonArray maps=base.value("maps").toArray();for(int i=0;i<maps.size();++i)if(maps.at(i).toObject().value("id").toString()==mapId){maps[i]=map;base["maps"]=maps;return;}maps.append(map);base["maps"]=maps;};
    auto reorderBase=[&](QJsonObject& base,const QJsonArray& order){const auto byId=mapsById(base.value("maps").toArray());QJsonArray maps;for(const auto& value:order){const QString id=value.toString();if(byId.contains(id))maps.append(byId.value(id));}if(maps.size()==byId.size())base["maps"]=maps;};

    // Um reset é um snapshot autoritativo do mapa naquela roomSeq. Só depois
    // de carregá-lo integralmente avançamos o cursor até a sequência indicada.
    for(const auto& resetValue:response.value("resetMaps").toArray()) {
        const auto mapObject=resetValue.toObject();const QString mapId=mapObject.value("id").toString();
        if(mapId.isEmpty())continue;
        bool localPending=false;for(const auto& pending:pendingRoomOperations){if(pending.toObject().value("map").toString()==mapId){localPending=true;break;}}
        // Durante um rebase, o snapshot atualiza somente a base/cursor. O mapa
        // visível e as operações locais continuam intactos e serão reenviados
        // sobre a sequência autoritativa da sala.
        if(localPending&&roomRebaseMaps.contains(mapId)){
            upsertBaseMap(serverBase,mapObject);upsertBaseMap(localBase,mapObject);
            if(seqs.contains(mapId))roomCursors[mapId]=qint64(seqs.value(mapId).toDouble());
            QJsonArray rebased;
            for(const auto& value:pendingRoomOperations){
                auto op=value.toObject();
                if(op.value("map").toString()==mapId){
                    originatedRoomOperationIds.remove(op.value("id").toString());
                    op.remove("id");op.remove("client");op.remove("cursor");op.remove("baseSeq");
                }
                rebased.append(op);
            }
            pendingRoomOperations=rebased;pendingRoomResets.remove(mapId);roomRebaseMaps.remove(mapId);
            successfulResets.insert(mapId);continue;
        }
        // Fora do rebase, um snapshot nunca pode sobrescrever uma alteração
        // local que ainda não recebeu ACK.
        if(localPending){failedMaps.insert(mapId);continue;}
        core::MapDoc incoming;QString error;if(!core::io::loadProjectMapPayload(ed,mapObject,&incoming,&error)){failedMaps.insert(mapId);continue;}
        const int index=ed.mapIndexById(mapId);
        if(index>=0){const QString activeLayer=ed.docs[index].activeLayerId;incoming.activeLayerId=activeLayer;incoming.activeLayerIdx=ed.docs[index].activeLayerIdx;ed.docs[index]=std::move(incoming);}
        else {ed.docs.push_back(std::move(incoming));docsChangedNeeded=true;}
        successfulResets.insert(mapId);pendingRoomResets.remove(mapId);touched.insert(mapId);docsChangedNeeded=true;
        if(seqs.contains(mapId))roomCursors[mapId]=qint64(seqs.value(mapId).toDouble());
        if(ed.doc()&&ed.doc()->id==mapId){activeChanged=true;activeLayersChanged=true;activeSelectionChanged=true;}
    }

    for(const auto& value:response.value("operations").toArray()) {
        const auto envelope=value.toObject();const QString mapId=envelope.value("map").toString();const qint64 seq=qint64(envelope.value("seq").toDouble());
        if(mapId.isEmpty()||successfulResets.contains(mapId)||failedMaps.contains(mapId))continue;
        const qint64 current=roomCursors.value(mapId,0);
        if(seq<=current)continue; // duplicada por watch + envio
        if(seq!=current+1){failedMaps.insert(mapId);continue;} // nunca pule um evento desconhecido

        const QJsonObject operation=envelope.value("operation").toObject();
        const QString operationId=operation.value("id").toString();
        const QString kind=operation.value("kind").toString();
        const bool own=!operationId.isEmpty()&&originatedRoomOperationIds.contains(operationId);
        const bool applied=own||applyRoomOperation(envelope);
        if(!applied){failedMaps.insert(mapId);continue;}

        roomCursors[mapId]=seq;
        if(!own){
            if(kind!="map.delete"&&kind!="map.order")touched.insert(mapId);
            if(isProjectStructureOperation(kind)||kind=="map.replace")docsChangedNeeded=true;
            if(isProjectStructureOperation(kind))remoteStructureChanged=true;
            if(kind=="map.delete"||kind=="map.order"){activeChanged=true;activeLayersChanged=true;activeSelectionChanged=true;}
            else if(ed.doc()&&ed.doc()->id==mapId){
                activeChanged=true;
                if(kind=="map.create"||kind=="map.replace"||kind=="layer.replace")activeLayersChanged=true;
                if(kind=="map.create"||kind=="map.replace"||kind=="layer.replace"||kind=="objects.patch")activeSelectionChanged=true;
            }
        }
    }

    // Nunca usamos roomSeqs como atalho para "parece que recebemos tudo".
    // Se houve qualquer falha/gap, o cursor fica exatamente na última operação
    // aplicada e pedimos um snapshot autoritativo daquele mapa.
    if(!failedMaps.isEmpty()){
        for(const QString& mapId:failedMaps){
            bool localPending=false;for(const auto& pending:pendingRoomOperations)if(pending.toObject().value("map").toString()==mapId){localPending=true;break;}
            pendingRoomResets.insert(mapId);
            if(localPending)roomRebaseMaps.insert(mapId);
        }
        emit statusChanged(tr("Reconciliando %1 mapa(s) — alterações locais preservadas…").arg(failedMaps.size()));scheduleRoomFlush(0);
    }

    for(const auto& id:acked)originatedRoomOperationIds.remove(id);

    // Atualiza a base local com ACKs estruturais sem esperar o próximo ciclo.
    for(const auto& op:ackedOperations){
        const QString kind=op.value("kind").toString(),mapId=op.value("map").toString();
        if(kind=="map.create"){const auto accepted=op.value("value").toObject();upsertBaseMap(serverBase,accepted);upsertBaseMap(localBase,accepted);}
        else if(kind=="map.delete"){removeBaseMap(serverBase,mapId);removeBaseMap(localBase,mapId);roomCursors.remove(mapId);pendingRoomResets.remove(mapId);}
        else if(kind=="map.order"){reorderBase(serverBase,op.value("order").toArray());reorderBase(localBase,op.value("order").toArray());}
    }

    if(!touched.isEmpty()||remoteStructureChanged){
        ed.projectDirty=true;
        for(const auto& mapId:touched)if(auto* changed=ed.mapById(mapId)){changed->dirty=true;observedHistoryPtr[mapId]=changed->historyPtr;observedHistoryRevision[mapId]=changed->historyRevision;}
        emit ed.projectChanged();
    }
    QSet<QString> bases=touched;bases.unite(ackMaps);
    for(const auto& mapId:bases)if(!mapId.isEmpty())updateTeamBaseForMap(mapId);

    if(activeChanged){if(activeLayersChanged)emit ed.layersChanged();if(activeSelectionChanged)emit ed.selectionChanged();emit ed.mapChanged();}
    if(docsChangedNeeded)emit ed.docsChanged();

    if(!ackedCreates.isEmpty())scheduleMapStructureReconcile(0);
    if(remoteRevision>revision)scheduleStructureRefresh(0);
    showStatus(response);return failedMaps.isEmpty();
}

bool CollaborationClient::exchangeRoomNow(bool showProgress) {
    if(!attached())return false;
    ensureRoomWatch();QString error;
    const bool ok=drainRealtimeForPublication(&error);
    if(!ok&&showProgress)QMessageBox::warning(window,tr("Equipe"),error);
    return ok;
}
void CollaborationClient::roomTick() {
    // Team Protocol 9: este método é somente o canal de ENVIO. Ele é
    // disparado por debounce logo após uma transação do Editor, não por polling.
    if(!attached()||(pendingRoomOperations.isEmpty()&&pendingRoomResets.isEmpty()))return;
    if(syncState.roomConflict)return;
    if(busy){scheduleRoomFlush(120);return;}
    if(roomRequestPending||rasterDownloading)return;

    QJsonArray batch;QSet<QString> maps;const int take=qMin(400,pendingRoomOperations.size());
    for(int i=0;i<take;++i){
        auto op=pendingRoomOperations.at(i).toObject();
        const QString mapId=op.value("map").toString();
        maps.insert(mapId);
        // O snapshot autoritativo precisa chegar antes de reenviarmos as
        // operações locais deste mapa.
        if(roomRebaseMaps.contains(mapId))continue;
        if(op.value("id").toString().isEmpty()){
            const QString opId=clientId+":"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            op["id"]=opId;originatedRoomOperationIds.insert(opId);
            op["client"]=clientId;op["cursor"]=double(roomCursors.value(mapId,0));
            if(op.value("kind").toString()=="map.replace")op["baseSeq"]=double(roomCursors.value(mapId,0));
            pendingRoomOperations[i]=op;
        }
        batch.append(op);
    }
    const QString activeId=ed.doc()?ed.doc()->id:QString();
    const QString active=mapKnownToServer(activeId)?activeId:QString();
    if(!active.isEmpty())maps.insert(active);
    QJsonObject cursors;for(const auto& mapId:maps)if(mapKnownToServer(mapId))cursors[mapId]=double(roomCursors.value(mapId,0));
    QJsonArray resets;for(const auto& mapId:pendingRoomResets)if(mapKnownToServer(mapId))resets.append(mapId);

    roomRequestPending=true;
    const quint64 requestEpoch=teamEpoch;const QString requestedProject=project,requestedToken=token;
    QUrl blobBase=server;blobBase.setPath("/v1/projects/"+project+"/asset-blobs");
    const QJsonObject outgoingBody{{"map",active},{"cursors",cursors},{"operations",batch},{"resetMaps",resets}};
    rasterTransport->upload(outgoingBody,blobBase,token,[this,requestEpoch,requestedProject,requestedToken,maps](bool prepared,const QJsonValue& wire){
    if(teamEpoch!=requestEpoch||project!=requestedProject||token!=requestedToken)return;
    if(!prepared){roomRequestPending=false;emit statusChanged(tr("Envio de imagens pendente — alterações preservadas"));scheduleRoomFlush(900);return;}
    QUrl url=server;url.setPath("/v1/projects/"+project+"/room-exchange");
    QNetworkRequest req(url);req.setTransferTimeout(8000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    const auto bytes=QJsonDocument(wire.toObject()).toJson(QJsonDocument::Compact);
    auto* reply=network.post(req,bytes);roomRequestPending=true;QElapsedTimer elapsed;elapsed.start();
    QTimer::singleShot(10000,reply,[reply]{if(reply->isRunning())reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,elapsed,requestEpoch,requestedProject,requestedToken,maps]{
        if(teamEpoch!=requestEpoch||project!=requestedProject||token!=requestedToken){reply->deleteLater();return;}
        roomRequestPending=false;
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto replyError=reply->error();
        const QString replyErrorString=reply->errorString();
        const auto document=QJsonDocument::fromJson(reply->readAll());
        const bool ok=replyError==QNetworkReply::NoError&&status==200&&document.isObject();
        const QString reason=document.object().value("error").toString(replyErrorString);
        const bool reachable=replyError==QNetworkReply::NoError&&status>0;
        reply->deleteLater();if(!attached())return;
        setConnectionState(reachable?(elapsed.elapsed()>=1500?QStringLiteral("slow"):QStringLiteral("online")):QStringLiteral("offline"));
        if(ok&&!busy)applyRoomResponse(document.object());
        else if(!ok){
            const QString normalized=reason.toLower();
            const bool staleStructure=status==409&&(normalized.contains(QStringLiteral("mudou na sala"))||
                normalized.contains(QStringLiteral("receba as alterações"))||normalized.contains(QStringLiteral("estrutura mudou"))||
                normalized.contains(QStringLiteral("ordem")));
            if(staleStructure&&!maps.isEmpty()){
                for(const auto& mapId:maps)if(!mapId.isEmpty()&&mapKnownToServer(mapId)){roomRebaseMaps.insert(mapId);pendingRoomResets.insert(mapId);}
                emit statusChanged(tr("Reconciliando alterações da equipe — suas alterações locais foram preservadas…"));
            } else if(status==409||status==403){syncState.roomConflict=true;syncState.checkpointRequired=true;emit statusChanged(tr("Conflito de sala — alterações locais preservadas: %1").arg(reason));}
            else emit statusChanged(tr("Sala temporariamente sem sincronizar — %1").arg(reason));
        }

        if((!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty())&&!syncState.roomConflict)
            scheduleRoomFlush(ok?35:850);
        ensureRoomWatch();
    });
    });
}

void CollaborationClient::stopRoomWatch() {
    roomWatchRetryTimer.stop();
    if(roomWatchReply){
        auto* reply=roomWatchReply;roomWatchReply=nullptr;roomWatchPending=false;roomWatchMap.clear();
        disconnect(reply,nullptr,this,nullptr);reply->abort();reply->deleteLater();
    } else {roomWatchPending=false;roomWatchMap.clear();}
}
void CollaborationClient::restartRoomWatch() {
    if(!attached())return;
    const QString activeId=ed.doc()?ed.doc()->id:QString();
    const QString active=mapKnownToServer(activeId)?activeId:QString();
    if(roomWatchPending&&roomWatchMap==active)return;
    stopRoomWatch();
    roomWatchRetryTimer.start(0);
}
void CollaborationClient::ensureRoomWatch() {
    if(!attached()||roomWatchPending||rasterDownloading)return;
    if(busy){if(!roomWatchRetryTimer.isActive())roomWatchRetryTimer.start(120);return;}

    const quint64 requestEpoch=teamEpoch;const QString requestedProject=project,requestedToken=token;
    const QString activeId=ed.doc()?ed.doc()->id:QString();
    const QString active=mapKnownToServer(activeId)?activeId:QString();
    QJsonObject cursors;if(!active.isEmpty())cursors[active]=double(roomCursors.value(active,0));
    QUrl url=server;url.setPath("/v1/projects/"+project+"/room-watch");
    QNetworkRequest req(url);req.setTransferTimeout(28000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    const auto bytes=QJsonDocument(QJsonObject{{"map",active},{"cursors",cursors},{"waitMs",20000}}).toJson(QJsonDocument::Compact);
    auto* reply=network.post(req,bytes);roomWatchReply=reply;roomWatchPending=true;roomWatchMap=active;QElapsedTimer elapsed;elapsed.start();
    QTimer::singleShot(30000,reply,[reply]{if(reply->isRunning())reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,requestEpoch,requestedProject,requestedToken,elapsed]{
        if(reply!=roomWatchReply){reply->deleteLater();return;}
        roomWatchReply=nullptr;roomWatchPending=false;roomWatchMap.clear();
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto document=QJsonDocument::fromJson(reply->readAll());
        const bool ok=reply->error()==QNetworkReply::NoError&&status==200&&document.isObject();
        const QString reason=document.object().value("error").toString(reply->errorString());
        reply->deleteLater();
        if(!attached()||teamEpoch!=requestEpoch||project!=requestedProject||token!=requestedToken)return;
        if(ok){
            setConnectionState(elapsed.elapsed()>=22000?QStringLiteral("slow"):QStringLiteral("online"));
            // Se uma operação modal/sincronização estiver usando a base do
            // projeto, não mutamos o Editor no meio dela. Como o cursor local
            // não avança, o próximo watch receberá a mesma mudança novamente.
            if(!busy)applyRoomResponse(document.object());
            roomWatchRetryTimer.start(0);
        } else {
            setConnectionState(QStringLiteral("offline"));
            if(status!=0&&status!=408)emit statusChanged(tr("Tempo real reconectando — %1").arg(reason));
            roomWatchRetryTimer.start(850);
        }
    });
}

bool CollaborationClient::synchronize() {
    if(!attached()){
        QMessageBox::information(window,tr("Equipe"),tr("Abra um projeto da equipe primeiro."));
        return false;
    }
    if(serverProtocol!=9){
        QMessageBox::warning(window,tr("Equipe"),tr("Este projeto exige um servidor Team Protocol 9."));
        return false;
    }
    if(role=="viewer"&&(ed.projectDirty||syncState.localChangesPending||syncState.checkpointRequired)){
        if(!saveLocalCopy())return false;
        QMessageBox::information(window,tr("Equipe"),tr("Sua cópia local foi salva. Este acesso é somente leitura."));
        return true;
    }
    if(mapStructureTimer.isActive()){
        mapStructureTimer.stop();
        reconcileMapStructure();
    }
    QString error;
    if(!drainRealtimeForPublication(&error)){
        if(!error.isEmpty())QMessageBox::warning(window,tr("Equipe"),error);
        return false;
    }
    // A estrutura remota usa o mesmo versão salva somente para leitura. Escritas
    // percorrem operações de sala, recursos e assets; não há commit paralelo.
    if(remoteRevision>revision){
        QJsonObject response;
        if(!request("projects/"+project+"/sync",{{"revision",revision}},response,false,false,
                    tr("Atualizando a estrutura do projeto…"),20,75))return false;
        const auto delta=response.value("delta").toObject();
        bool valid=false;const auto payload=applyDelta(serverBase,delta,&valid);
        if(!valid){QMessageBox::warning(window,tr("Equipe"),tr("A atualização recebida é inválida. Sua cópia local foi preservada."));return false;}
        response["payload"]=payload;
        if(!applyIncremental(delta,payload,response))return false;
        revision=response.value("revision").toInt(revision);remoteRevision=revision;serverBase=payload;
    }
    if(!saveLocalCopy())return false;
    localBase=snapshot();syncState.checkpointRequired=false;syncState.structureDirty=false;
    persistLink();showStatus(QJsonObject{{"revision",revision},{"presence",lastPresence}});
    return true;
}
bool CollaborationClient::drainRealtimeForPublication(QString* error,int timeoutMs) {
    if(!attached())return true;
    if(syncState.resourceConflict||syncState.roomConflict){
        if(error)*error=syncState.roomConflict
            ? tr("Existe um conflito de edição em tempo real. Suas alterações locais foram preservadas; reconcilie a sala antes de atualizar o RPG Maker.")
            : tr("Existe um conflito no mesmo Tileset/Autotile. Revise esse recurso antes de atualizar o RPG Maker.");
        return false;
    }

    // A atualização do RPG Maker é uma barreira: nenhuma fila live pode ficar "quase"
    // enviada. Mantemos o event loop ativo para que upload/download, room ACK
    // e resource ACK terminem sem congelar a janela principal.
    beginProgress(tr("Preparando atualização"),tr("Finalizando alterações em tempo real…"));
    updateProgress(8);

    QElapsedTimer elapsed;elapsed.start();
    QEventLoop loop;
    QTimer pulse; pulse.setInterval(35);
    QTimer deadline; deadline.setSingleShot(true); deadline.setInterval(qMax(5000,timeoutMs));
    bool timedOut=false;

    auto kick=[this]{
        if(mapStructureTimer.isActive()){
            mapStructureTimer.stop();
            reconcileMapStructure();
        }
        if((!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty())&&!roomRequestPending&&!syncState.roomConflict)scheduleRoomFlush(0);
        if((assetDirty||!pendingAssetOperations.isEmpty()||remoteAssetSeq>assetCursor)&&!assetRequestPending)
            scheduleAssetSync({},0);
        if((resourceDirty||!deferredRemoteResources.isEmpty())&&!resourceRequestPending&&!syncState.resourceConflict)
            scheduleResourceSync(0);
    };
    auto liveIdle=[this]{
        const bool roomIdle=!rasterDownloading&&pendingRoomOperations.isEmpty()&&pendingRoomResets.isEmpty()&&!roomRequestPending&&!syncState.roomConflict;
        const bool assetIdle=!assetIndexPending&&!assetDirty&&!assetRequestPending&&pendingAssetOperations.isEmpty()&&remoteAssetSeq<=assetCursor;
        const bool resourceIdle=!atlasPreparationPending&&!resourceDirty&&!resourceRequestPending&&deferredRemoteResources.isEmpty();
        const bool structureIdle=!mapStructureTimer.isActive()&&!structureRefreshPending;
        return roomIdle&&assetIdle&&resourceIdle&&structureIdle;
    };

    connect(&pulse,&QTimer::timeout,&loop,[&]{
        if(!attached()||syncState.resourceConflict||syncState.roomConflict){loop.quit();return;}
        kick();
        QString label;
        if(roomRequestPending||!pendingRoomOperations.isEmpty()||!pendingRoomResets.isEmpty())label=tr("Enviando alterações do mapa…");
        else if(assetRequestPending||assetDirty||!pendingAssetOperations.isEmpty()||remoteAssetSeq>assetCursor)label=tr("Sincronizando arquivos do projeto…");
        else if(resourceRequestPending||resourceDirty||!deferredRemoteResources.isEmpty())label=tr("Sincronizando Tilesets/Autotiles…");
        else if(structureRefreshPending||mapStructureTimer.isActive())label=tr("Confirmando estrutura do projeto…");
        if(!label.isEmpty())updateProgress(qMin(88,12+int(elapsed.elapsed()/1200)),label);
        if(liveIdle())loop.quit();
    });
    connect(&deadline,&QTimer::timeout,&loop,[&]{timedOut=true;loop.quit();});

    kick();pulse.start();deadline.start();
    if(!liveIdle())loop.exec();
    pulse.stop();deadline.stop();

    const bool ok=!timedOut&&attached()&&!syncState.resourceConflict&&!syncState.roomConflict&&liveIdle();
    if(ok)updateProgress(100,tr("Alterações em tempo real confirmadas."));
    endProgress();
    if(ok)return true;

    if(error){
        if(syncState.roomConflict)*error=tr("Existe um conflito de edição em tempo real. Suas alterações locais foram preservadas; reconcilie a sala antes de atualizar o RPG Maker.");
        else if(syncState.resourceConflict)*error=tr("Existe um conflito no mesmo Tileset/Autotile. Revise esse recurso antes de atualizar o RPG Maker.");
        else if(timedOut)*error=tr("O servidor demorou demais para confirmar todas as alterações. Sua cópia local foi preservada; verifique a conexão e tente publicar novamente.");
        else *error=tr("Não foi possível confirmar todas as alterações em tempo real antes da atualização do RPG Maker.");
    }
    return false;
}

bool CollaborationClient::preparePublication(int& version) {
    if(!attached())return false;
    if(role!="admin"){
        QMessageBox::information(window,tr("Atualizar RPG Maker"),tr("A atualização do RPG Maker é feita pelo administrador."));return false;
    }
    // A mesma barreira usada por Ctrl+S confirma sala, recursos, assets e
    // estrutura. Publicar não mantém um segundo caminho de sincronização.
    if(!synchronize())return false;
    if(hasPending()){
        QMessageBox::warning(window,tr("Atualização pendente"),tr("O servidor ainda não confirmou todas as alterações. Aguarde e tente novamente."));return false;
    }
    QJsonObject response;if(!request("projects/"+project+"/status",{},response,true))return false;
    if(response.value("revision").toInt()!=revision){
        QMessageBox::information(window,tr("Nova versão disponível"),tr("A equipe enviou alterações durante a preparação. Salve novamente antes de atualizar o RPG Maker."));return false;
    }
    version=revision;return true;
}
bool CollaborationClient::verifyPublication(int version) {
    if(!attached() || hasPending())return false;
    QJsonObject response;
    if(!request("projects/"+project+"/status",{},response,true))return false;
    if(response.value("revision").toInt()!=version) {
        QMessageBox::information(window,tr("Nova versão disponível"),tr("A equipe enviou alterações. Abra a atualização novamente para revisar a versão atual."));return false;
    }
    return true;
}
void CollaborationClient::heartbeat() {
    if(!attached())return;
    if(!pendingRoomOperations.isEmpty())scheduleRoomFlush(0);
    if(assetDirty||!pendingAssetOperations.isEmpty())scheduleAssetSync({},0);
    if(resourceDirty||!deferredRemoteResources.isEmpty())scheduleResourceSync(0);
    ensureRoomWatch();
}
void CollaborationClient::showStatus(const QJsonObject& response) {
    if(response.contains("presence"))lastPresence=response.value("presence").toArray();
    QSet<QString> onlineUsers;
    int sameRoom=0;const QString activeMap=ed.doc()?ed.doc()->id:QString();
    for(const auto& value:lastPresence) {
        const auto item=value.toObject();
        onlineUsers.insert(item.value("user").toString());
        if(!activeMap.isEmpty()&&item.value("map").toString()==activeMap)++sameRoom;
    }
    const int remote=response.value("revision").toInt(revision);remoteRevision=qMax(remoteRevision,remote);
    const bool transferring=roomRequestPending||assetRequestPending||resourceRequestPending;
    QString state=syncState.roomConflict?tr("Conflito de sala — precisa de atenção")
        :syncState.resourceConflict?tr("Conflito de recursos — precisa de atenção")
        :transferring?tr("Sincronizando alterações"):hasPending()?tr("Alterações aguardando envio"):tr("Sala sincronizada");
    if(remote>revision)state=tr("Nova versão disponível");
    QStringList parts{state,tr("versão %1").arg(revision)};
    if(sameRoom>0)parts<<tr("%1 na sala deste mapa").arg(sameRoom);
    else if(!onlineUsers.isEmpty())parts<<tr("%1 online").arg(onlineUsers.size());
    emit statusChanged(parts.join(" · "));
}
void CollaborationClient::people() {
    if(!attached()){QMessageBox::information(window,tr("Equipe"),tr("Abra um projeto da equipe primeiro."));return;}
    if(!exchangeRoomNow(false))return;
    const QJsonObject response{{"revision",remoteRevision},{"presence",lastPresence}};
    showStatus(response);const auto presence=lastPresence;
    if(presence.isEmpty()){QMessageBox::information(window,tr("Pessoas conectadas"),tr("Nenhuma presença ativa foi encontrada."));return;}
    QStringList lines;
    for(const auto& value:presence) {
        const auto item=value.toObject();QString location=tr("sem mapa aberto");const auto id=item.value("map").toString();
        if(!id.isEmpty()) {location=id;for(const auto& d:ed.docs)if(d.id==id){location=d.name;break;}}
        lines<<QStringLiteral("%1 — %2%3").arg(item.value("user").toString(),location,item.value("mine").toBool()?tr(" (você)"):QString());
    }
    QMessageBox::information(window,tr("Pessoas conectadas — %1").arg(serverName),lines.join("\n"));
}
void CollaborationClient::history() {
    if(!attached())return;
    QJsonObject response;if(!request("projects/"+project+"/history",{},response,true))return;
    const auto versions=response.value("versions").toArray();QStringList labels;
    for(const auto& value:versions){auto v=value.toObject();labels<<tr("Versão %1 — %2 — %3").arg(v.value("revision").toInt()).arg(v.value("user").toString(),QDateTime::fromSecsSinceEpoch(qint64(v.value("created").toDouble())).toString("dd/MM/yyyy HH:mm"));}
    bool ok=false;QString choice=QInputDialog::getItem(window,tr("Histórico — baixar uma cópia"),tr("Versão"),labels,0,false,&ok);
    if(!ok||labels.isEmpty())return;
    int number=versions[labels.indexOf(choice)].toObject().value("revision").toInt();
    if(!request("projects/"+project+"/versions/"+QString::number(number),{},response,true))return;
    QString path=QFileDialog::getSaveFileName(window,tr("Guardar versão"),QString("Versao-%1.ludo").arg(number),tr("Projeto LUDO (*.ludo)"));if(path.isEmpty())return;
    if(!path.endsWith(".ludo",Qt::CaseInsensitive))path+=".ludo";
    QSaveFile file(path);const auto bytes=QJsonDocument(response.value("payload").toObject()).toJson();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())QMessageBox::warning(window,tr("Histórico"),tr("Não foi possível guardar esta versão."));
}
void CollaborationClient::detachProject() {
    ++teamEpoch;
    stopRoomWatch();roomFlushTimer.stop();mapStructureTimer.stop();structureRefreshTimer.stop();
    project.clear();localPath.clear();identity.clear();revision=0;remoteRevision=0;serverBase={};localBase={};lastPresence={};syncState.checkpointRequired=false;
    assetSyncTimer.stop();resourceSyncTimer.stop();
    pendingRoomOperations=QJsonArray();pendingRoomResets.clear();roomRebaseMaps.clear();originatedRoomOperationIds.clear();roomCursors.clear();observedHistoryPtr.clear();observedHistoryRevision.clear();roomRequestPending=false;roomWatchPending=false;roomWatchMap.clear();syncState.roomConflict=false;
    assetDirty=false;assetFullScanRequested=false;assetRequestPending=false;assetStateInitialized=false;preserveLocalAssetsOnBootstrap=false;assetCursor=0;pendingAssetCursor=0;remoteAssetSeq=0;pendingAssetPaths.clear();sharedAssets.clear();bootstrapLocalAssets.clear();bootstrapLocalKnownAssetIds.clear();bootstrapLocalMissingAssetIds.clear();bootstrapLocalAssetDatabase={};pendingAssetOperations=QJsonArray();pendingAssetUploads.clear();pendingAssetDownloads.clear();pendingAssetNextState.clear();pendingAssetUploadIndex=0;pendingAssetDownloadIndex=0;
    atlasPreparationPending=false;atlasPreparationReady=false;++atlasPreparationRevision;
    rasterDownloading=false;assetIndexPending=false;assetIndexReady=false;++assetIndexRevision;
    resourceDirty=false;resourceRequestPending=false;syncState.resourceConflict=false;resourceCursor=0;pendingResourceSnapshot={};pendingResourceOperationId.clear();deferredRemoteResources={};deferredRemoteResourceSeq=0;deferredRemoteResourceRevision=0;
    structureRefreshPending=false;teamTilesetSources.clear();teamTilesetBackingIds.clear();
    if(!token.isEmpty())emit statusChanged(tr("Conectado — escolha um projeto da equipe"));
}
void CollaborationClient::disconnectServer() {
    ++teamEpoch;
    stopRoomWatch();roomFlushTimer.stop();mapStructureTimer.stop();structureRefreshTimer.stop();
    const bool showProgress=!token.isEmpty()||attached();
    if(showProgress)beginProgress(tr("Desconectando equipe"),attached()&&ed.projectDirty
        ?tr("Salvando sua cópia local…"):tr("Encerrando a sessão…"));
    if(attached()) {
        updateProgress(20);
        if(!saveLocalCopy()){if(showProgress)endProgress();return;}
    }
    QJsonObject response;
    if(!token.isEmpty())request("logout",{},response,false,true,tr("Encerrando a sessão…"),55,90);
    timer.stop();token.clear();role.clear();project.clear();localPath.clear();identity.clear();serverBase={};localBase={};lastPresence={};revision=0;remoteRevision=0;syncState.checkpointRequired=false;serverProtocol=1;serverName.clear();serverId.clear();
    assetSyncTimer.stop();resourceSyncTimer.stop();
    pendingRoomOperations=QJsonArray();pendingRoomResets.clear();roomRebaseMaps.clear();originatedRoomOperationIds.clear();roomCursors.clear();observedHistoryPtr.clear();observedHistoryRevision.clear();roomRequestPending=false;roomWatchPending=false;roomWatchMap.clear();syncState.roomConflict=false;
    assetDirty=false;assetFullScanRequested=false;assetRequestPending=false;assetStateInitialized=false;preserveLocalAssetsOnBootstrap=false;assetCursor=0;pendingAssetCursor=0;remoteAssetSeq=0;pendingAssetPaths.clear();sharedAssets.clear();bootstrapLocalAssets.clear();bootstrapLocalKnownAssetIds.clear();bootstrapLocalMissingAssetIds.clear();bootstrapLocalAssetDatabase={};pendingAssetOperations=QJsonArray();pendingAssetUploads.clear();pendingAssetDownloads.clear();pendingAssetNextState.clear();pendingAssetUploadIndex=0;pendingAssetDownloadIndex=0;
    atlasPreparationPending=false;atlasPreparationReady=false;++atlasPreparationRevision;
    rasterDownloading=false;assetIndexPending=false;assetIndexReady=false;++assetIndexRevision;
    resourceDirty=false;resourceRequestPending=false;syncState.resourceConflict=false;resourceCursor=0;pendingResourceSnapshot={};pendingResourceOperationId.clear();deferredRemoteResources={};deferredRemoteResourceSeq=0;deferredRemoteResourceRevision=0;
    structureRefreshPending=false;teamTilesetSources.clear();teamTilesetBackingIds.clear();
    setConnectionState(QStringLiteral("offline"));
    if(showProgress){updateProgress(100,tr("Desconectado."));endProgress();}
    emit statusChanged(tr("Desconectado — trabalhando na cópia local"));
}
}
