#include "ProjectIO.h"
#include "serialization/AtomicProjectFile.h"
#include "serialization/ProjectHeaderSerializer.h"
#include "serialization/ProjectSchema.h"
#include "AssetWorkflow.h"
#include "ResourceManager.h"
#include "Version.h"
#include "Renderer.h"
#include "TilesetOps.h"
#include "TilesetCatalog.h"
#include "ProjectMigration.h"
#include "io/MapProjectPayload.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QSet>
#include <functional>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QMetaType>
#include <algorithm>
#include <utility>

namespace core { namespace io {

// ------------------------------------------------------------- utilidades
namespace {
const QByteArray kProtectedProjectMagicV1("LUDOCRYPT1\n");
const QByteArray kProtectedProjectMagicV2("LUDOCRYPT2\n");

QByteArray ludoProtectionStream(const qsizetype size)
{
    QByteArray stream;stream.reserve(int(size));
    const QByteArray seed=QCryptographicHash::hash(QByteArrayLiteral("LUDO_GAME_ENGINE_EXPORT_V1"),QCryptographicHash::Sha256);
    quint64 counter=0;while(stream.size()<size){stream+=QCryptographicHash::hash(seed+QByteArray::number(counter++),QCryptographicHash::Sha256);}stream.truncate(int(size));return stream;
}
QByteArray xorProjectPayload(const QByteArray& bytes){QByteArray out=bytes;const QByteArray stream=ludoProtectionStream(out.size());for(qsizetype i=0;i<out.size();++i)out[i]=char(uchar(out.at(i))^uchar(stream.at(i)));return out;}

quint32 rotl32(quint32 v,int n){return (v<<n)|(v>>(32-n));}
quint32 read32(const uchar* p){return quint32(p[0])|(quint32(p[1])<<8)|(quint32(p[2])<<16)|(quint32(p[3])<<24);}
void write32(uchar* p,quint32 v){p[0]=uchar(v);p[1]=uchar(v>>8);p[2]=uchar(v>>16);p[3]=uchar(v>>24);}
void quarter(quint32& a,quint32& b,quint32& c,quint32& d){a+=b;d^=a;d=rotl32(d,16);c+=d;b^=c;b=rotl32(b,12);a+=b;d^=a;d=rotl32(d,8);c+=d;b^=c;b=rotl32(b,7);}
QByteArray chacha20(const QByteArray& input,const QByteArray& key,const QByteArray& nonce)
{
    if(key.size()!=32||nonce.size()!=12)return {};
    const uchar* k=reinterpret_cast<const uchar*>(key.constData());const uchar* n=reinterpret_cast<const uchar*>(nonce.constData());
    QByteArray out=input;quint32 counter=1;
    for(qsizetype pos=0;pos<input.size();pos+=64,++counter){quint32 st[16]={0x61707865,0x3320646e,0x79622d32,0x6b206574,read32(k),read32(k+4),read32(k+8),read32(k+12),read32(k+16),read32(k+20),read32(k+24),read32(k+28),counter,read32(n),read32(n+4),read32(n+8)};quint32 w[16];for(int i=0;i<16;++i)w[i]=st[i];for(int r=0;r<10;++r){quarter(w[0],w[4],w[8],w[12]);quarter(w[1],w[5],w[9],w[13]);quarter(w[2],w[6],w[10],w[14]);quarter(w[3],w[7],w[11],w[15]);quarter(w[0],w[5],w[10],w[15]);quarter(w[1],w[6],w[11],w[12]);quarter(w[2],w[7],w[8],w[13]);quarter(w[3],w[4],w[9],w[14]);}uchar block[64];for(int i=0;i<16;++i)write32(block+i*4,w[i]+st[i]);const qsizetype take=qMin<qsizetype>(64,input.size()-pos);for(qsizetype i=0;i<take;++i)out[pos+i]=char(uchar(input.at(pos+i))^block[i]);}
    return out;
}
QByteArray hmacSha256(QByteArray key,const QByteArray& data)
{
    if(key.size()>64)key=QCryptographicHash::hash(key,QCryptographicHash::Sha256);key=key.leftJustified(64,char(0),true);QByteArray opad(64,char(0x5c)),ipad(64,char(0x36));for(int i=0;i<64;++i){opad[i]=char(uchar(opad[i])^uchar(key[i]));ipad[i]=char(uchar(ipad[i])^uchar(key[i]));}return QCryptographicHash::hash(opad+QCryptographicHash::hash(ipad+data,QCryptographicHash::Sha256),QCryptographicHash::Sha256);
}
QByteArray deriveKey(const QByteArray& salt){return QCryptographicHash::hash(QByteArrayLiteral("LUDO_GAME_ENGINE_EXPORT_V2::PROJECT_ASSET_CONTAINER")+salt,QCryptographicHash::Sha256);}
bool constantTimeEquals(const QByteArray&a,const QByteArray&b){if(a.size()!=b.size())return false;uchar diff=0;for(int i=0;i<a.size();++i)diff|=uchar(a[i])^uchar(b[i]);return diff==0;}

QByteArray maybeUnprotectProjectPayload(const QByteArray& bytes)
{
    if(bytes.startsWith(kProtectedProjectMagicV2)){
        const QByteArray body=bytes.mid(kProtectedProjectMagicV2.size());if(body.size()<60)return {};
        const QByteArray salt=body.left(16),nonce=body.mid(16,12),tag=body.mid(28,32),cipher=body.mid(60);const QByteArray key=deriveKey(salt);const QByteArray auth=hmacSha256(key,salt+nonce+cipher);if(!constantTimeEquals(tag,auth))return {};return chacha20(cipher,key,nonce);
    }
    if(bytes.startsWith(kProtectedProjectMagicV1))return xorProjectPayload(bytes.mid(kProtectedProjectMagicV1.size()));
    return bytes;
}


} // namespace

// ------------------------------------------------------------ serializacao
// Efeitos associados diretamente ao recurso de Tile/Autotile.
// Mantemos a serializacao aqui (e nao na UI) para que projetos antigos
// continuem abrindo mesmo quando nao possuem o campo resourceEffects.
static QJsonArray serializeResourceEffects(const QVector<TileResourceEffectBinding>& effects)
{
    QJsonArray out;
    for (const TileResourceEffectBinding& effect : effects) {
        const QString type = effect.type.trimmed();
        if (type.isEmpty()) continue;

        QJsonObject obj;
        obj.insert(QStringLiteral("type"), type);
        if (!effect.preset.trimmed().isEmpty())
            obj.insert(QStringLiteral("preset"), effect.preset.trimmed());
        if (!effect.enabled)
            obj.insert(QStringLiteral("enabled"), false);

        if (!effect.properties.isEmpty()) {
            QJsonObject props;
            for (auto it = effect.properties.constBegin(); it != effect.properties.constEnd(); ++it)
                props.insert(it.key(), it.value());
            if (!props.isEmpty()) obj.insert(QStringLiteral("properties"), props);
        }
        out.append(obj);
    }
    return out;
}

static QVector<TileResourceEffectBinding> deserializeResourceEffects(const QJsonValue& value)
{
    QVector<TileResourceEffectBinding> out;
    if (!value.isArray()) return out;

    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue& item : array) {
        if (!item.isObject()) continue;
        const QJsonObject obj = item.toObject();

        TileResourceEffectBinding effect;
        effect.type = obj.value(QStringLiteral("type")).toString().trimmed();
        if (effect.type.isEmpty()) continue;
        effect.preset = obj.value(QStringLiteral("preset")).toString().trimmed();
        effect.enabled = !obj.contains(QStringLiteral("enabled"))
            || obj.value(QStringLiteral("enabled")).toBool(true);

        const QJsonObject props = obj.value(QStringLiteral("properties")).toObject();
        for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
            if (it.value().isString()) effect.properties.insert(it.key(), it.value().toString());
            else if (it.value().isBool()) effect.properties.insert(it.key(), it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            else if (it.value().isDouble()) effect.properties.insert(it.key(), QString::number(it.value().toDouble(), 'g', 15));
            else if (!it.value().isNull() && !it.value().isUndefined())
                effect.properties.insert(it.key(), QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact)));
        }
        out.push_back(effect);
    }
    return out;
}

static QJsonObject tileRefToJson(const TileRef& t)
{
    QJsonObject o;
    o["tilesetIdx"] = t.tilesetIdx;
    o["tx"] = t.tx;
    o["ty"] = t.ty;
    if (!t.wangSetId.isEmpty()) {
        o["wangSetId"] = t.wangSetId;
        o["wangColorId"] = t.wangColorId;
    }
    return o;
}

static TileRef tileRefFromJson(const QJsonObject& o)
{
    TileRef t;
    t.tilesetIdx = o.value("tilesetIdx").toInt(-1);
    t.tx = o.value("tx").toInt();
    t.ty = o.value("ty").toInt();
    t.wangSetId = o.value("wangSetId").toString();
    t.wangColorId = o.contains("wangColorId") ? o.value("wangColorId").toInt(-1) : -1;
    return t;
}

/// Uma celula pode ser: null | objeto unico | array (pilha) — igual ao JS.
QJsonValue cellToJson(const Cell& c)
{
    if (c.isEmpty()) return QJsonValue(QJsonValue::Null);
    if (c.size() == 1) return tileRefToJson(c.first());
    QJsonArray arr;
    for (const TileRef& t : c) arr.append(tileRefToJson(t));
    return arr;
}

Cell cellFromJson(const QJsonValue& v)
{
    Cell c;
    if (v.isNull() || v.isUndefined()) return c;
    if (v.isArray()) {
        for (const QJsonValue& e : v.toArray())
            if (e.isObject()) {
                const TileRef t = tileRefFromJson(e.toObject());
                if (t.isValid()) c.push_back(t);
            }
        return c;
    }
    if (v.isObject()) {
        const TileRef t = tileRefFromJson(v.toObject());
        if (t.isValid()) c.push_back(t);
    }
    return c;
}

static QJsonArray serializeRasterFilters(const QVector<RasterLayerFilter>& filters)
{
    QJsonArray out;
    for (const RasterLayerFilter& f : filters) {
        QJsonObject o;
        o["id"] = f.id; o["type"] = f.type; o["enabled"] = f.enabled;
        o["radius"] = f.radius; o["strength"] = f.strength; o["angle"] = f.angle; o["quality"] = f.quality;
        o["amount"] = f.amount; o["scale"] = f.scale; o["seed"] = f.seed; o["monochrome"] = f.monochrome;
        o["distance"] = f.distance; o["spread"] = f.spread; o["opacity"] = f.opacity;
        o["color"] = f.color.name(QColor::HexArgb);
        out.append(o);
    }
    return out;
}

static QVector<RasterLayerFilter> deserializeRasterFilters(const QJsonValue& value)
{
    QVector<RasterLayerFilter> out;
    if (!value.isArray()) return out;
    for (const QJsonValue& v : value.toArray()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        RasterLayerFilter f;
        f.id = o.value("id").toString(idGen());
        f.type = o.value("type").toString(QStringLiteral("gaussianBlur"));
        f.enabled = o.value("enabled").toBool(true);
        f.radius = qBound(0.0, o.value("radius").toDouble(4.0), 128.0);
        f.strength = qBound(0.0, o.value("strength").toDouble(1.0), 1.0);
        f.angle = qBound(-180.0, o.value("angle").toDouble(0.0), 180.0);
        f.quality = qBound(0, o.value("quality").toInt(1), 2);
        f.amount = qBound(0.0, o.value("amount").toDouble(0.12), 1.0);
        f.scale = qBound(1.0, o.value("scale").toDouble(2.0), 64.0);
        f.seed = o.value("seed").toInt(1337);
        f.monochrome = o.value("monochrome").toBool(true);
        f.distance = qBound(0.0, o.value("distance").toDouble(3.0), 64.0);
        f.spread = qBound(0.0, o.value("spread").toDouble(1.0), 32.0);
        f.opacity = qBound(0.0, o.value("opacity").toDouble(0.35), 1.0);
        const QColor c(o.value("color").toString(QStringLiteral("#ff000000")));
        f.color = c.isValid() ? c : QColor(0, 0, 0, 255);
        out.push_back(f);
    }
    return out;
}

static QJsonObject serializeNode(const LayerPtr& n)
{
    QJsonObject o;
    o["id"] = n->id;
    o["name"] = n->name;
    o["visible"] = n->visible;
    o["opacity"] = n->opacity;
    o["blendMode"] = n->blendMode;
    o["locked"] = n->locked;
    o["uiColor"] = n->uiColor.isValid() ? QJsonValue(n->uiColor.name()) : QJsonValue(QJsonValue::Null);
    o["offsetx"] = n->offsetx;
    o["offsety"] = n->offsety;
    o["collapsed"] = n->collapsed;
    o["zMode"] = n->zMode;
    o["depthLevel"] = n->depthLevel;
    o["alphaLock"] = n->alphaLock;
    if (n->parallaxLayer) o["parallaxLayer"] = true;
    // Mantém a configuração preparada mesmo quando a pessoa desativa a
    // Camada Visual temporariamente. Reativar não deve apagar movimento,
    // spritesheet ou clima visual.
    o["parallaxFactorX"] = n->parallaxFactorX;
    o["parallaxFactorY"] = n->parallaxFactorY;
    o["parallaxSpeedX"] = n->parallaxSpeedX;
    o["parallaxSpeedY"] = n->parallaxSpeedY;
    o["parallaxRepeatX"] = n->parallaxRepeatX;
    o["parallaxRepeatY"] = n->parallaxRepeatY;
    o["parallaxMotionPreset"] = n->parallaxMotionPreset;
    o["parallaxOscillationX"] = n->parallaxOscillationX;
    o["parallaxOscillationY"] = n->parallaxOscillationY;
    o["parallaxOscillationSpeed"] = n->parallaxOscillationSpeed;
    o["parallaxSmoothMotion"] = n->parallaxSmoothMotion;
    o["parallaxAnimationEnabled"] = n->parallaxAnimationEnabled;
    o["parallaxAnimationColumns"] = n->parallaxAnimationColumns;
    o["parallaxAnimationRows"] = n->parallaxAnimationRows;
    o["parallaxAnimationFrames"] = n->parallaxAnimationFrames;
    o["parallaxAnimationFps"] = n->parallaxAnimationFps;
    o["parallaxAnimationPingPong"] = n->parallaxAnimationPingPong;
    o["parallaxEffectPreset"] = n->parallaxEffectPreset;
    o["parallaxEffectStrength"] = n->parallaxEffectStrength;
    o["parallaxEffectSpeed"] = n->parallaxEffectSpeed;

    switch (n->type) {
    case LayerType::Group: {
        o["type"] = "group";
        if (!n->imageFilters.isEmpty()) o["imageFilters"] = serializeRasterFilters(n->imageFilters);
        QJsonArray ch;
        for (const LayerPtr& c : n->children) ch.append(serializeNode(c));
        o["children"] = ch;
        break;
    }
    case LayerType::Image: {
        o["type"] = "imagelayer";
        o["imagewidth"] = n->imagewidth;
        o["imageheight"] = n->imageheight;
        o["imageSrc"] = imageToDataUri(n->image);
        o["scaleX"] = n->imageScaleX;
        o["scaleY"] = n->imageScaleY;
        o["rotation"] = n->imageRotation;
        o["flipX"] = n->imageFlipX;
        o["flipY"] = n->imageFlipY;
        if (n->imageRepeatX) o["repeatX"] = true;
        if (n->imageRepeatY) o["repeatY"] = true;
        if (n->reflectionLayer) {
            o["reflectionLayer"] = true;
            o["reflectionPreset"] = n->reflectionPreset;
            o["reflectionOpacity"] = n->reflectionOpacity;
            o["reflectionBlur"] = n->reflectionBlur;
            o["reflectionWave"] = n->reflectionWave;
        }
        o["filter"] = n->imageFilter;
        o["referenceOnly"] = n->imageReferenceOnly;
        o["paintLayer"] = n->imagePaintLayer;
        o["maskEnabled"] = n->imageMaskEnabled;
        if (!n->imageMask.isNull()) o["maskImageSrc"] = imageToDataUri(n->imageMask);
        if (!n->imageFilters.isEmpty()) o["imageFilters"] = serializeRasterFilters(n->imageFilters);
        if (!n->maskFilters.isEmpty()) o["maskFilters"] = serializeRasterFilters(n->maskFilters);
        if (!n->imagePath.isEmpty()) o["source"] = n->imagePath;
        if (n->isMask) {
            o["isMask"] = true;
            o["maskShowBase"] = n->maskShowBase;
            QJsonArray ch;
            for (const LayerPtr& c : n->children) ch.append(serializeNode(c));
            o["children"] = ch;
        }
        break;
    }
    case LayerType::Tile: {
        o["type"] = "tilelayer";
        o["isMask"] = n->isMask;
        o["maskShowBase"] = n->maskShowBase;
        o["tileWidth"] = n->tileWidth;
        o["tileHeight"] = n->tileHeight;
        o["cols"] = n->cols;
        o["rows"] = n->rows;
        o["maskEnabled"] = n->imageMaskEnabled;
        if (!n->imageMask.isNull()) o["maskImageSrc"] = imageToDataUri(n->imageMask);
        if (!n->imageFilters.isEmpty()) o["imageFilters"] = serializeRasterFilters(n->imageFilters);
        if (!n->maskFilters.isEmpty()) o["maskFilters"] = serializeRasterFilters(n->maskFilters);
        QJsonArray rows;
        for (const QVector<Cell>& row : n->data2D) {
            QJsonArray r;
            for (const Cell& c : row) r.append(cellToJson(c));
            rows.append(r);
        }
        o["data2D"] = rows;
        if (n->isMask) {
            QJsonArray ch;
            for (const LayerPtr& c : n->children) ch.append(serializeNode(c));
            o["children"] = ch;
        }
        break;
    }
    case LayerType::Object: {
        o["type"] = "objectgroup";
        o["isMask"] = n->isMask;
        o["maskShowBase"] = n->maskShowBase;
        QJsonArray objs;
        for (const MapObject& obj : n->objects) {
            QJsonObject j;
            j["id"] = obj.id;
            j["name"] = obj.name;
            j["type"] = obj.type;
            j["x"] = obj.x; j["y"] = obj.y; j["w"] = obj.w; j["h"] = obj.h;
            j["rotation"] = obj.rotation;
            j["rotationFilter"] = obj.rotationFilter;
            j["scaleFilter"] = obj.scaleFilter;
            j["visible"] = obj.visible;
            j["stampW"] = obj.stampW;
            j["stampH"] = obj.stampH;
            if (!obj.properties.isEmpty()) {
                QJsonObject props;
                for (auto it = obj.properties.cbegin(); it != obj.properties.cend(); ++it)
                    props.insert(it.key(), it.value());
                j["properties"] = props;
            }
            QJsonArray tiles;
            for (const TileRef& t : obj.tiles) tiles.append(tileRefToJson(t));
            j["tiles"] = tiles;
            objs.append(j);
        }
        o["objects"] = objs;
        if (n->isMask) {
            QJsonArray ch;
            for (const LayerPtr& c : n->children) ch.append(serializeNode(c));
            o["children"] = ch;
        }
        break;
    }
    }
    return o;
}

static LayerPtr deserializeNode(const QJsonObject& o)
{
    const QString type = o.value("type").toString(QStringLiteral("tilelayer"));
    LayerPtr n(new Layer);
    n->id = o.value("id").toString(idGen());
    n->name = o.value("name").toString();
    n->visible = o.value("visible").toBool(true);
    n->opacity = o.value("opacity").toDouble(1.0);
    n->blendMode = o.value("blendMode").toString(QStringLiteral("source-over"));
    n->locked = o.value("locked").toBool(false);
    const QJsonValue col = o.value("uiColor");
    if (col.isString()) n->uiColor = QColor(col.toString());
    n->offsetx = o.value("offsetx").toInt();
    n->offsety = o.value("offsety").toInt();
    n->collapsed = o.value("collapsed").toBool(false);
    n->zMode = o.value("zMode").toString(QStringLiteral("below"));
    n->depthLevel = qBound(0, o.value("depthLevel").toInt(), 1);
    n->alphaLock = o.value("alphaLock").toBool(false);
    n->isMask = o.value("isMask").toBool(false);
    n->maskShowBase = o.value("maskShowBase").toBool(true);
    n->parallaxLayer = o.value("parallaxLayer").toBool(false);
    n->parallaxFactorX = qBound(-4.0, o.value("parallaxFactorX").toDouble(0.5), 4.0);
    n->parallaxFactorY = qBound(-4.0, o.value("parallaxFactorY").toDouble(0.5), 4.0);
    n->parallaxSpeedX = qBound(-2000.0, o.value("parallaxSpeedX").toDouble(), 2000.0);
    n->parallaxSpeedY = qBound(-2000.0, o.value("parallaxSpeedY").toDouble(), 2000.0);
    n->parallaxRepeatX = o.value("parallaxRepeatX").toBool(false);
    n->parallaxRepeatY = o.value("parallaxRepeatY").toBool(false);
    n->parallaxMotionPreset = o.value("parallaxMotionPreset").toString(QStringLiteral("custom"));
    n->parallaxOscillationX = qBound(0.0, o.value("parallaxOscillationX").toDouble(), 4096.0);
    n->parallaxOscillationY = qBound(0.0, o.value("parallaxOscillationY").toDouble(), 4096.0);
    n->parallaxOscillationSpeed = qBound(0.0, o.value("parallaxOscillationSpeed").toDouble(1.0), 20.0);
    n->parallaxSmoothMotion = o.value("parallaxSmoothMotion").toBool(true);
    n->parallaxAnimationEnabled = o.value("parallaxAnimationEnabled").toBool(false);
    n->parallaxAnimationColumns = qBound(1, o.value("parallaxAnimationColumns").toInt(1), 64);
    n->parallaxAnimationRows = qBound(1, o.value("parallaxAnimationRows").toInt(1), 64);
    const int maxVisualFrames = n->parallaxAnimationColumns * n->parallaxAnimationRows;
    n->parallaxAnimationFrames = qBound(1, o.value("parallaxAnimationFrames").toInt(maxVisualFrames), maxVisualFrames);
    n->parallaxAnimationFps = qBound(0.1, o.value("parallaxAnimationFps").toDouble(8.0), 60.0);
    n->parallaxAnimationPingPong = o.value("parallaxAnimationPingPong").toBool(false);
    n->parallaxEffectPreset = o.value("parallaxEffectPreset").toString(QStringLiteral("none"));
    n->parallaxEffectStrength = qBound(0.0, o.value("parallaxEffectStrength").toDouble(1.0), 1.0);
    n->parallaxEffectSpeed = qBound(0.0, o.value("parallaxEffectSpeed").toDouble(1.0), 10.0);

    if (type == QLatin1String("group")) {
        n->type = LayerType::Group;
        n->imageFilters = deserializeRasterFilters(o.value("imageFilters"));
        // Grupo aceita apenas pós-processamento de contato; filtros raster de
        // conteúdo pertencem às camadas-filhas.
        for (int i = n->imageFilters.size() - 1; i >= 0; --i)
            if (n->imageFilters[i].type != QLatin1String("contactShadow")) n->imageFilters.removeAt(i);
    } else if (type == QLatin1String("imagelayer")) {
        n->type = LayerType::Image;
        n->image = dataUriToImage(o.value("imageSrc").toString());
        n->imagePath = o.value("source").toString();
        n->imagewidth = o.value("imagewidth").toInt(n->image.width());
        n->imageheight = o.value("imageheight").toInt(n->image.height());
        n->imageScaleX = qBound(0.01, o.value("scaleX").toDouble(1.0), 100.0);
        n->imageScaleY = qBound(0.01, o.value("scaleY").toDouble(1.0), 100.0);
        n->imageRotation = o.value("rotation").toDouble(0.0);
        n->imageFlipX = o.value("flipX").toBool(false);
        n->imageFlipY = o.value("flipY").toBool(false);
        n->imageRepeatX = o.value("repeatX").toBool(false);
        n->imageRepeatY = o.value("repeatY").toBool(false);
        n->reflectionLayer = o.value("reflectionLayer").toBool(false);
        n->reflectionPreset = o.value("reflectionPreset").toString(QStringLiteral("still"));
        n->reflectionOpacity = qBound(0, o.value("reflectionOpacity").toInt(70), 100);
        n->reflectionBlur = qBound(0, o.value("reflectionBlur").toInt(2), 24);
        n->reflectionWave = qBound(0, o.value("reflectionWave").toInt(4), 32);
        n->imageFilter = o.value("filter").toString(QStringLiteral("nearest"));
        if (n->imageFilter != QLatin1String("nearest") && n->imageFilter != QLatin1String("bilinear"))
            n->imageFilter = QStringLiteral("nearest");
        // Projetos antigos só possuíam Image Layer de referência.
        n->imageReferenceOnly = o.contains("referenceOnly")
            ? o.value("referenceOnly").toBool(true) : true;
        n->imagePaintLayer = o.value("paintLayer").toBool(false);
        n->imageMaskEnabled = o.value("maskEnabled").toBool(false);
        n->imageMask = dataUriToImage(o.value("maskImageSrc").toString());
        if (!n->imageMask.isNull())
            n->imageMask = n->imageMask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        n->imageFilters = deserializeRasterFilters(o.value("imageFilters"));
        n->maskFilters = deserializeRasterFilters(o.value("maskFilters"));
        if (n->imagePaintLayer) n->imageReferenceOnly = false;
    } else if (type == QLatin1String("objectgroup")) {
        n->type = LayerType::Object;
        for (const QJsonValue& v : o.value("objects").toArray()) {
            const QJsonObject j = v.toObject();
            MapObject obj;
            obj.id = j.value("id").toString(idGen());
            obj.name = j.value("name").toString();
            obj.type = j.value("type").toString();
            obj.x = j.value("x").toDouble();
            obj.y = j.value("y").toDouble();
            obj.w = j.value("w").toDouble();
            obj.h = j.value("h").toDouble();
            obj.rotation = j.value("rotation").toDouble();
            obj.rotationFilter = j.value("rotationFilter").toString(QStringLiteral("rotsprite"));
            if (obj.rotationFilter != QLatin1String("nearest") &&
                obj.rotationFilter != QLatin1String("smooth") &&
                obj.rotationFilter != QLatin1String("rotsprite"))
                obj.rotationFilter = QStringLiteral("rotsprite");
            obj.scaleFilter = j.value("scaleFilter").toString(QStringLiteral("nearest"));
            if (obj.scaleFilter != QLatin1String("xbr") &&
                obj.scaleFilter != QLatin1String("nearest") &&
                obj.scaleFilter != QLatin1String("smooth"))
                obj.scaleFilter = QStringLiteral("nearest");
            obj.visible = j.value("visible").toBool(true);
            obj.stampW = j.value("stampW").toInt(1);
            obj.stampH = j.value("stampH").toInt(1);
            const QJsonObject props = j.value("properties").toObject();
            for (auto it = props.constBegin(); it != props.constEnd(); ++it)
                obj.properties.insert(it.key(), it.value().toString());
            for (const QJsonValue& t : j.value("tiles").toArray())
                obj.tiles.push_back(tileRefFromJson(t.toObject()));
            n->objects.push_back(obj);
        }
    } else {
        n->type = LayerType::Tile;
        n->tileWidth = qMax(1, o.value("tileWidth").toInt(32));
        n->tileHeight = qMax(1, o.value("tileHeight").toInt(32));
        const QJsonArray rows = o.value("data2D").toArray();
        n->rows = o.value("rows").toInt(rows.size());
        n->cols = o.value("cols").toInt(rows.isEmpty() ? 0 : rows.first().toArray().size());
        n->allocGrid();
        n->imageMaskEnabled = o.value("maskEnabled").toBool(false);
        n->imageMask = dataUriToImage(o.value("maskImageSrc").toString());
        if (!n->imageMask.isNull())
            n->imageMask = n->imageMask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        n->imageFilters = deserializeRasterFilters(o.value("imageFilters"));
        // Tile Layer suporta apenas processamento de sombra de contato.
        for (int i = n->imageFilters.size() - 1; i >= 0; --i)
            if (n->imageFilters[i].type != QLatin1String("contactShadow")) n->imageFilters.removeAt(i);
        n->maskFilters = deserializeRasterFilters(o.value("maskFilters"));
        for (int y = 0; y < qMin(n->rows, rows.size()); ++y) {
            const QJsonArray r = rows[y].toArray();
            for (int x = 0; x < qMin(n->cols, r.size()); ++x)
                n->data2D[y][x] = cellFromJson(r[x]);
        }
    }
    for (const QJsonValue& v : o.value("children").toArray())
        n->children.push_back(deserializeNode(v.toObject()));
    return n;
}

// ------------------------------------------------------ Asset Database 3.24
namespace {

bool looksLikeProjectAssetPath(const QString& value)
{
    const QString normalized = AssetDatabase::normalizePath(value);
    return normalized.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive);
}

void collectAssetReferences(const QJsonValue& value, const AssetDatabase& database,
                            const QString& owner, QJsonArray& output)
{
    if (value.isString()) {
        const QString path = AssetDatabase::normalizePath(value.toString());
        if (!looksLikeProjectAssetPath(path)) return;
        const QString id = database.idForPath(path);
        if (id.isEmpty()) return;
        QJsonObject ref;
        ref.insert(QStringLiteral("id"), id);
        ref.insert(QStringLiteral("path"), path);
        ref.insert(QStringLiteral("owner"), owner.isEmpty() ? QStringLiteral("/") : owner);
        if (const AssetRecord* record = database.recordById(id))
            ref.insert(QStringLiteral("type"), record->type);
        output.append(ref);
        return;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
            collectAssetReferences(array.at(i), database,
                                   owner + QLatin1Char('/') + QString::number(i), output);
        return;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        // Asset Database / Bloco B: algumas referências modernas podem manter
        // somente o GUID (por exemplo FootstepSound depois de um move/rename).
        // O grafo de exportação precisa reconhecer o asset mesmo sem depender
        // de um caminho legado duplicado no payload.
        const QString explicitAssetId = object.value(QStringLiteral("assetId")).toString().trimmed();
        if (!explicitAssetId.isEmpty()) {
            if (const AssetRecord* record = database.recordById(explicitAssetId)) {
                if (!record->missing) {
                    const QString path = database.pathForId(explicitAssetId);
                    if (!path.isEmpty()) {
                        QJsonObject ref;
                        ref.insert(QStringLiteral("id"), explicitAssetId);
                        ref.insert(QStringLiteral("path"), path);
                        ref.insert(QStringLiteral("owner"), owner.isEmpty() ? QStringLiteral("/assetId")
                                                                            : owner + QStringLiteral("/assetId"));
                        ref.insert(QStringLiteral("type"), record->type);
                        output.append(ref);
                    }
                }
            }
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            collectAssetReferences(it.value(), database,
                                   owner + QLatin1Char('/') + it.key(), output);
    }
}

QJsonValue remapAssetStrings(const QJsonValue& value, const AssetDatabase& database,
                             const QHash<QString, QString>& stableByLegacyPath)
{
    if (value.isString()) {
        const QString original = value.toString();
        if (!looksLikeProjectAssetPath(original)) return value;
        const QString normalized = AssetDatabase::normalizePath(original);
        const QString stable = stableByLegacyPath.value(normalized.toLower());
        if (!stable.isEmpty()) return stable;
        const QString canonical = database.canonicalPath(normalized);
        return canonical.isEmpty() ? value : QJsonValue(canonical);
    }
    if (value.isArray()) {
        QJsonArray result;
        const QJsonArray input = value.toArray();
        for (const QJsonValue& child : input)
            result.append(remapAssetStrings(child, database, stableByLegacyPath));
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const QJsonObject input = value.toObject();
        for (auto it = input.constBegin(); it != input.constEnd(); ++it)
            result.insert(it.key(), remapAssetStrings(it.value(), database, stableByLegacyPath));
        return result;
    }
    return value;
}

void rewriteLayerAssetPaths(const LayerPtr& layer, const QString& oldNormalized,
                            const QString& newNormalized)
{
    if (!layer) return;
    if (AssetDatabase::normalizePath(layer->imagePath).compare(oldNormalized, Qt::CaseInsensitive) == 0)
        layer->imagePath = newNormalized;
    for (const LayerPtr& child : layer->children)
        rewriteLayerAssetPaths(child, oldNormalized, newNormalized);
}

} // namespace

void rewriteEditorAssetPath(Editor& ed, const QString& oldProjectRelativePath,
                            const QString& newProjectRelativePath)
{
    const QString oldNormalized = AssetDatabase::normalizePath(oldProjectRelativePath);
    const QString newNormalized = AssetDatabase::normalizePath(newProjectRelativePath);
    if (oldNormalized.isEmpty() || newNormalized.isEmpty() ||
        oldNormalized.compare(newNormalized, Qt::CaseInsensitive) == 0) return;

    auto rewrite = [&](QString& path) {
        if (AssetDatabase::normalizePath(path).compare(oldNormalized, Qt::CaseInsensitive) == 0)
            path = newNormalized;
    };

    for (Tileset& tileset : ed.tilesets) {
        rewrite(tileset.sourcePath);
        for (CombinedSource& source : tileset.combinedSources) rewrite(source.sourcePath);
    }
    for (MapDoc& document : ed.docs) {
        rewrite(document.map.panoramaPath);
        for (const LayerPtr& layer : document.layers)
            rewriteLayerAssetPaths(layer, oldNormalized, newNormalized);
        document.dirty = true;
    }

    ed.projectDirty = true;
    ed.resources().notifyAssetsChanged({oldNormalized, newNormalized});
    emit ed.tilesetsChanged();
    emit ed.layersChanged();
    emit ed.mapChanged();
    emit ed.projectChanged();
}

// -------------------------------------------------------------- payload
QJsonObject buildProjectMapPayload(const MapDoc& d)
{
    QJsonObject m;
    m["id"] = d.id;
    m["name"] = d.name;
    if (!d.parentId.isEmpty()) m["parentId"] = d.parentId;
    if (!d.variationBaseId.isEmpty()) {
        m["variationBaseId"] = d.variationBaseId;
        m["variationName"] = d.variationName;
    }
    if (d.rpgMakerMapId > 0) m["rpgMakerMapId"] = d.rpgMakerMapId;
    if (d.rpgMakerImported) m["rpgMakerImported"] = true;
    QJsonObject mi;
    mi["width"] = d.map.width;
    mi["height"] = d.map.height;
    mi["tileWidth"] = d.map.tileWidth;
    mi["tileHeight"] = d.map.tileHeight;
    mi["background"] = d.map.background.name();
    mi["depthEnabled"] = d.map.depthEnabled;
    mi["depthScale"] = d.map.depthScale;
    mi["depthStartLevel"] = d.map.depthStartLevel;
    mi["depthTransitions"] = d.map.depthTransitions;
    if (!d.map.panoramaPath.isEmpty()) mi["panoramaPath"] = d.map.panoramaPath;
    if (!d.map.panorama.isNull()) mi["panoramaSrc"] = imageToDataUri(d.map.panorama);
    if (!d.map.panorama.isNull() || !d.map.panoramaPath.isEmpty())
        mi["panoramaVisible"] = d.map.panoramaVisible;
    if (d.map.panoramaOpacity != 255) mi["panoramaOpacity"] = d.map.panoramaOpacity;
    if (d.map.panoramaFit) mi["panoramaFit"] = true;
    if (d.map.panoramaRepeat) mi["panoramaRepeat"] = true;
    m["map"] = mi;
    m["activeLayerIdx"] = d.activeLayerIdx;
    m["activeLayerId"] = d.activeLayerId;
    if (!d.reflectionSettings.isEmpty()) m["reflectionSettings"] = d.reflectionSettings;

    if (d.rpgMakerRegionsAuthored) {
        m["rpgMakerRegionsAuthored"] = true;
        QJsonObject regions;
        for (auto it = d.rpgMakerRegions.cbegin(); it != d.rpgMakerRegions.cend(); ++it) {
            const int x = MapDoc::regionX(it.key());
            const int y = MapDoc::regionY(it.key());
            if (!d.regionInBounds(x, y) || it.value() == 0) continue;
            regions.insert(QStringLiteral("%1,%2").arg(x).arg(y), int(it.value()));
        }
        m["rpgMakerRegions"] = regions;
    }
    QJsonArray layers;
    for (const LayerPtr& layer : d.layers) layers.append(serializeNode(layer));
    m["layers"] = layers;
    return m;
}

static MapInfo deserializeProjectMapInfo(const Editor& ed, const QJsonObject& mi)
{
    MapInfo info;
    info.width = qMax(1, mi.value("width").toInt(40));
    info.height = qMax(1, mi.value("height").toInt(30));
    info.tileWidth = qMax(1, mi.value("tileWidth").toInt(32));
    info.tileHeight = qMax(1, mi.value("tileHeight").toInt(32));
    info.background = QColor(mi.value("background").toString(QStringLiteral("#2e2e2e")));
    info.depthEnabled = mi.value("depthEnabled").toBool();
    info.depthScale = qBound(0.70, mi.value("depthScale").toDouble(0.90), 1.0);
    info.depthStartLevel = qBound(0, mi.value("depthStartLevel").toInt(), 1);
    info.depthTransitions = mi.value("depthTransitions").toArray();
    info.panoramaPath = mi.value("panoramaPath").toString();
    info.panorama = dataUriToImage(mi.value("panoramaSrc").toString());
    if (info.panorama.isNull() && !info.panoramaPath.isEmpty()) {
        const QFileInfo panoramaFile(info.panoramaPath);
        const QString absolute = panoramaFile.isAbsolute()
            ? panoramaFile.absoluteFilePath()
            : QDir(ed.projectRoot()).filePath(info.panoramaPath);
        info.panorama.load(absolute);
    }
    info.panoramaVisible = mi.value("panoramaVisible").toBool(!info.panorama.isNull());
    info.panoramaOpacity = qBound(0, mi.value("panoramaOpacity").toInt(255), 255);
    info.panoramaFit = mi.value("panoramaFit").toBool(false);
    info.panoramaRepeat = mi.value("panoramaRepeat").toBool(false);
    return info;
}

bool loadProjectMapPayload(const Editor& ed, const QJsonObject& mapObject,
                           MapDoc* output, QString* error)
{
    if (!output) {
        if (error) *error = QObject::tr("Destino de mapa inválido.");
        return false;
    }
    if (mapObject.isEmpty()) {
        if (error) *error = QObject::tr("Mapa recebido está vazio.");
        return false;
    }
    MapDoc map;
    // Mantém a compatibilidade do loader completo com projetos históricos
    // que ainda não possuíam id de mapa. Fluxos incrementais validam o id
    // antes de chamar este helper, pois precisam de identidade estável.
    map.id = mapObject.value("id").toString(idGen());
    map.name = mapObject.value("name").toString(QStringLiteral("Mapa"));
    map.parentId = mapObject.value("parentId").toString();
    map.variationBaseId = mapObject.value("variationBaseId").toString();
    map.variationName = mapObject.value("variationName").toString();
    map.rpgMakerMapId = qMax(0, mapObject.value("rpgMakerMapId").toInt());
    map.rpgMakerImported = mapObject.value("rpgMakerImported").toBool(false);
    map.map = deserializeProjectMapInfo(ed, mapObject.value("map").toObject());
    map.activeLayerIdx = mapObject.value("activeLayerIdx").toInt(0);
    map.activeLayerId = mapObject.value("activeLayerId").toString();
    map.reflectionSettings = mapObject.value("reflectionSettings").toObject();

    map.rpgMakerRegionsAuthored = mapObject.value("rpgMakerRegionsAuthored").toBool(
        mapObject.contains("rpgMakerRegions"));
    const QJsonValue regionValue = mapObject.value("rpgMakerRegions");
    if (regionValue.isObject()) {
        const QJsonObject regions = regionValue.toObject();
        for (auto it = regions.constBegin(); it != regions.constEnd(); ++it) {
            const QStringList xy = it.key().split(QLatin1Char(','));
            if (xy.size() != 2) continue;
            bool okX = false, okY = false;
            const int x = xy[0].toInt(&okX), y = xy[1].toInt(&okY);
            const int id = qBound(0, it.value().toInt(), 255);
            if (okX && okY && id > 0 && map.regionInBounds(x, y))
                map.rpgMakerRegions.insert(MapDoc::regionKey(x, y), quint8(id));
        }
    } else if (regionValue.isArray()) {
        const QJsonArray regions = regionValue.toArray();
        const int count = qMin(regions.size(), map.map.width * map.map.height);
        for (int i = 0; i < count; ++i) {
            const int id = qBound(0, regions.at(i).toInt(), 255);
            if (id <= 0) continue;
            const int x = i % map.map.width, y = i / map.map.width;
            map.rpgMakerRegions.insert(MapDoc::regionKey(x, y), quint8(id));
        }
    }
    for (const QJsonValue& layerValue : mapObject.value("layers").toArray())
        map.layers.push_back(deserializeNode(layerValue.toObject()));
    map.dirty = false;
    *output = std::move(map);
    return true;
}

QJsonObject buildProjectPayload(const Editor& ed)
{
    QJsonObject root = serialization::writeProjectHeader(ed);

    QJsonArray maps;
    for (const MapDoc& d : ed.docs) maps.append(buildProjectMapPayload(d));
    root["maps"] = maps;

    QJsonArray tss;
    for (const Tileset& ts : ed.tilesets) {
        QJsonObject t;
        t["id"] = ts.id;
        t["name"] = ts.name;
        t["category"] = ts.category;
        t["firstgid"] = ts.firstgid;
        t["imagewidth"] = ts.imagewidth;
        t["imageheight"] = ts.imageheight;
        t["tilewidth"] = ts.tilewidth;
        t["tileheight"] = ts.tileheight;
        t["spacing"] = ts.spacing;
        t["margin"] = ts.margin;
        t["columns"] = ts.columns;
        t["rows"] = ts.rows;
        t["tilecount"] = ts.tilecount;
        t["isVX512"] = ts.isVX512;
        if (ts.internalAutotileAtlas) t["internalAutotileAtlas"] = true;
        if (!ts.paletteVisible) t["paletteVisible"] = false;
        if (!ts.pageGroupId.trimmed().isEmpty()) t["pageGroupId"] = ts.pageGroupId;
        if (ts.pageIndex != 0) t["pageIndex"] = ts.pageIndex;
        if (ts.generatedFromBake) {
            t["generatedFromBake"] = true;
            if (!ts.bakeSourceLayerId.isEmpty()) t["bakeSourceLayerId"] = ts.bakeSourceLayerId;
        }
        if (ts.generatedFromSlope) {
            t["generatedFromSlope"] = true;
            if (!ts.slopeSourceLayerId.isEmpty()) t["slopeSourceLayerId"] = ts.slopeSourceLayerId;
            if (!ts.slopeAxis.isEmpty()) t["slopeAxis"] = ts.slopeAxis;
            t["slopeStep"] = ts.slopeStep;
        }
        if (ts.chromaApplied) {
            t["chromaColor"] = ts.chromaColor.name();
            t["chromaTolerance"] = ts.chromaTolerance;
        }
        t["src"] = imageToDataUri(ts.image);
        if (!ts.sourcePath.isEmpty()) {
            t["source"] = ts.sourcePath;
            if (ts.sourceTileX != 0) t["sourceTileX"] = ts.sourceTileX;
            if (ts.sourceTileY != 0) t["sourceTileY"] = ts.sourceTileY;
            // Tilesets/Autotiles 2.0 / Bloco 9: o caminho continua no payload
            // por compatibilidade, mas a identidade do Asset Database viaja
            // junto quando existe. Export/Dependency Index podem assim seguir
            // o asset por GUID mesmo depois de move/rename, sem criar um ID
            // paralelo dentro de Tileset.
            const QString sourceAssetId = ed.assetDatabase.idForPath(AssetDatabase::normalizePath(ts.sourcePath));
            if (!sourceAssetId.isEmpty()) t["assetId"] = sourceAssetId;
        }
        t["combined"] = ts.combined;
        t["combinedDirection"] = ts.combinedDirection;
        t["combinedTileCount"] = ts.combinedTileCount;
        if (ts.combined) {
            QJsonArray srcs;
            for (const CombinedSource& s : ts.combinedSources) {
                QJsonObject o;
                o["name"] = s.name; o["x"] = s.x; o["y"] = s.y;
                if (!s.sourcePath.isEmpty()) o["source"] = s.sourcePath;
                o["cols"] = s.cols; o["rows"] = s.rows; o["count"] = s.count;
                if (s.sourceTileX != 0) o["sourceTileX"] = s.sourceTileX;
                if (s.sourceTileY != 0) o["sourceTileY"] = s.sourceTileY;
                srcs.append(o);
            }
            t["combinedSources"] = srcs;
            QJsonObject cur; cur["x"] = ts.packCursorX; cur["y"] = ts.packCursorY;
            t["packCursor"] = cur;
        }
        if (!ts.animatedAutotiles.isEmpty()) {
            QJsonArray animations;
            for (const AnimatedAutotile& anim : ts.animatedAutotiles) {
                QJsonObject a;
                a["id"] = anim.id; a["name"] = anim.name;
                a["x"] = anim.baseX; a["y"] = anim.baseY;
                a["cols"] = anim.cols; a["rows"] = anim.rows;
                a["fps"] = anim.fps; a["loop"] = anim.loop;
                a["pingPong"] = anim.pingPong; a["synchronized"] = anim.synchronized;
                QJsonArray frames;
                for (const QPoint& origin : anim.frameOrigins) {
                    QJsonObject f; f["x"] = origin.x(); f["y"] = origin.y(); frames.append(f);
                }
                a["frames"] = frames;
                animations.append(a);
            }
            t["animatedAutotiles"] = animations;
        }
        if (!ts.tilePriorities.isEmpty()) {
            QJsonObject priorities;
            for (auto it = ts.tilePriorities.constBegin(); it != ts.tilePriorities.constEnd(); ++it) {
                const int value = clampi(it.value(), 0, 5);
                if (value > 0) priorities.insert(it.key(), value);
            }
            if (!priorities.isEmpty()) t["tilePriorities"] = priorities;
        }
        if (!ts.tileCollisionMasks.isEmpty()) {
            QJsonObject collisions;
            for (auto it = ts.tileCollisionMasks.constBegin(); it != ts.tileCollisionMasks.constEnd(); ++it) {
                const int mask = it.value() & 0x0f;
                if (mask) collisions.insert(it.key(), mask);
            }
            if (!collisions.isEmpty()) t["tileCollisionMasks"] = collisions;
        }
        if (!ts.tileProbabilities.isEmpty()) {
            QJsonObject probabilities;
            for (auto it = ts.tileProbabilities.constBegin(); it != ts.tileProbabilities.constEnd(); ++it)
                if (!qFuzzyCompare(it.value(), 1.0)) probabilities.insert(it.key(), it.value());
            if (!probabilities.isEmpty()) t["tileProbabilities"] = probabilities;
        }
        if (!ts.tileResourceEffects.isEmpty()) {
            QJsonObject bindings;
            for (auto it=ts.tileResourceEffects.constBegin(); it!=ts.tileResourceEffects.constEnd(); ++it) {
                const QJsonArray effects=serializeResourceEffects(it.value()); if(!effects.isEmpty()) bindings.insert(it.key(),effects);
            }
            if(!bindings.isEmpty()) t["tileResourceEffects"] = bindings;
        }
        tss.append(t);
    }
    root["tilesets"] = tss;

    // Autotiles sao recursos globais do projeto. `tilesetId` aponta apenas
    // para o backing fisico (normal legado ou atlas interno oculto).
    if (!ed.autotiles.isEmpty()) {
        QJsonArray autotiles;
        for (const TilesetAutotile& autotile : ed.autotiles) {
            QJsonObject a;
            a["id"] = autotile.id;
            a["name"] = autotile.name;
            if (!autotile.category.trimmed().isEmpty()) a["category"] = autotile.category.trimmed();
            a["tilesetId"] = autotile.tilesetId;
            a["x"] = autotile.baseX; a["y"] = autotile.baseY;
            a["cols"] = autotile.cols; a["rows"] = autotile.rows;
            if (!autotile.wangSetId.isEmpty()) a["wangSetId"] = autotile.wangSetId;
            if (autotile.wangColorId >= 0) a["wangColorId"] = autotile.wangColorId;
            if (!autotile.animatedAutotileId.isEmpty()) a["animatedAutotileId"] = autotile.animatedAutotileId;
            if (autotile.previewTx >= 0 && autotile.previewTy >= 0) {
                a["previewX"] = autotile.previewTx; a["previewY"] = autotile.previewTy;
            }
            a["extendAtMapBoundary"] = autotile.extendAtMapBoundary;
            const QJsonArray resourceEffects=serializeResourceEffects(autotile.resourceEffects);
            if(!resourceEffects.isEmpty()) a["resourceEffects"] = resourceEffects;
            autotiles.append(a);
        }
        root["autotiles"] = autotiles;
    }

    QJsonArray wss;
    for (const WangSet& ws : ed.wangSets) {
        QJsonObject w;
        w["id"] = ws.id;
        w["name"] = ws.name;
        w["type"] = ws.type;
        if (ws.hasIcon) {
            QJsonObject ic;
            ic["tilesetIdx"] = ws.iconTilesetIdx; ic["tx"] = ws.iconTx; ic["ty"] = ws.iconTy;
            w["icon"] = ic;
        }
        QJsonArray cs;
        for (const WangColor& c : ws.colors) {
            QJsonObject o;
            o["id"] = c.id; o["name"] = c.name; o["color"] = c.color.name();
            if (c.hasIcon) {
                QJsonObject ic;
                ic["tilesetIdx"] = c.iconTilesetIdx; ic["tx"] = c.iconTx; ic["ty"] = c.iconTy;
                o["icon"] = ic;
            }
            cs.append(o);
        }
        w["colors"] = cs;
        QJsonObject tiles;
        for (auto it = ws.tiles.constBegin(); it != ws.tiles.constEnd(); ++it) {
            QJsonObject d;
            const WangTileData& v = it.value();
            if (v.tl >= 0) d["tl"] = v.tl;
            if (v.t  >= 0) d["t"]  = v.t;
            if (v.tr >= 0) d["tr"] = v.tr;
            if (v.r  >= 0) d["r"]  = v.r;
            if (v.br >= 0) d["br"] = v.br;
            if (v.b  >= 0) d["b"]  = v.b;
            if (v.bl >= 0) d["bl"] = v.bl;
            if (v.l  >= 0) d["l"]  = v.l;
            // Mesma chave da versao web, para os projetos serem intercambiaveis.
            if (v.isolatedColorId >= 0) d["_isolatedColorId"] = v.isolatedColorId;
            tiles.insert(it.key(), d);
        }
        w["tiles"] = tiles;
        wss.append(w);
    }
    root["wangSets"] = wss;

    QJsonObject star, coll, prob;
    // Campo legado para leitores antigos: toda prioridade 1..5 continua
    // aparecendo como ★. A fonte de verdade moderna vive dentro do Tileset.
    for (int tsIdx = 0; tsIdx < ed.tilesets.size(); ++tsIdx) {
        const Tileset& ts = ed.tilesets.at(tsIdx);
        for (auto it = ts.tilePriorities.constBegin(); it != ts.tilePriorities.constEnd(); ++it) {
            if (clampi(it.value(), 0, 5) <= 0) continue;
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() != 2) continue;
            star.insert(tileKey(tsIdx, parts[0].toInt(), parts[1].toInt()), true);
        }
    }
    // Campos raiz permanecem como ESPELHO de compatibilidade para leitores
    // antigos. A fonte de verdade 2.0 vive dentro de cada Tileset.
    for (int tsIdx = 0; tsIdx < ed.tilesets.size(); ++tsIdx) {
        const Tileset& ts = ed.tilesets.at(tsIdx);
        for (auto it = ts.tileCollisionMasks.constBegin(); it != ts.tileCollisionMasks.constEnd(); ++it) {
            const int mask = it.value() & 0x0f;
            if (!mask) continue;
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() == 2)
                coll.insert(tileKey(tsIdx, parts[0].toInt(), parts[1].toInt()), mask);
        }
        for (auto it = ts.tileProbabilities.constBegin(); it != ts.tileProbabilities.constEnd(); ++it) {
            if (qFuzzyCompare(it.value(), 1.0)) continue;
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() == 2)
                prob.insert(tileKey(tsIdx, parts[0].toInt(), parts[1].toInt()), it.value());
        }
    }
    root["starTiles"] = star;
    root["collisionTiles"] = coll;
    root["tileProbability"] = prob;

    QJsonArray pool;
    for (const RandomEntry& e : ed.randomPool) {
        QJsonObject o;
        o["tilesetIdx"] = e.tilesetIdx;
        o["x"] = e.x; o["y"] = e.y; o["w"] = e.w; o["h"] = e.h;
        QJsonArray tiles;
        for (int i = 0; i < e.tiles.size(); ++i) {
            QJsonObject t = tileRefToJson(e.tiles[i]);
            t["dx"] = e.offsets[i].x();
            t["dy"] = e.offsets[i].y();
            tiles.append(t);
        }
        o["tiles"] = tiles;
        pool.append(o);
    }
    root["randomPool"] = pool;

    if (!ed.savedStamps.isEmpty()) {
        QJsonArray patterns;
        for (const SavedStamp& saved : ed.savedStamps) {
            if (!saved.stamp.valid()) continue;
            QJsonObject o;
            o["id"] = saved.id;
            o["name"] = saved.name;
            o["w"] = saved.stamp.w;
            o["h"] = saved.stamp.h;
            QJsonArray tiles;
            for (int i = 0; i < saved.stamp.tiles.size() && i < saved.stamp.offsets.size(); ++i) {
                QJsonObject t = tileRefToJson(saved.stamp.tiles[i]);
                t["dx"] = saved.stamp.offsets[i].x();
                t["dy"] = saved.stamp.offsets[i].y();
                tiles.append(t);
            }
            o["tiles"] = tiles;
            patterns.append(o);
        }
        if (!patterns.isEmpty()) root["patterns"] = patterns;
    }

    root = mapproject::sanitize(root);
    QJsonArray assetReferences;
    collectAssetReferences(root, ed.assetDatabase, QString(), assetReferences);
    root["assetReferences"] = assetReferences;
    root["assetDatabase"] = ed.assetDatabase.toJson();
    return root;
}

bool loadProject(Editor& ed, const QString& path, QString* error, ProjectLoadMode mode)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Não foi possível abrir %1: %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError perr{};
    const QByteArray rawProject = maybeUnprotectProjectPayload(f.readAll());
    const QJsonDocument jdoc = QJsonDocument::fromJson(rawProject, &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !jdoc.isObject()) {
        if (error) *error = QObject::tr("Projeto Ludo inválido: %1").arg(perr.errorString());
        return false;
    }
    QJsonObject root = jdoc.object();
    bool migratedProject = false;
    // Bloco 5 / RC2.19: fronteira explícita de migração. Projetos antigos sem
    // assinatura/formatVersion continuam aceitos como ProjectFormat 1; um
    // formato futuro é recusado em vez de ser interpretado parcialmente.
    // A validação aceita LudoMapProject e projetos LudoEngineProject antigos para migração.
    int loadedProjectFormat = 1;
    if (!serialization::validateProjectEnvelope(root, version::ProjectFormat,
                                                &loadedProjectFormat, &migratedProject, error))
        return false;
    const bool hadAssetDatabase = root.contains(QStringLiteral("assetDatabase"));
    ed.projectPath = path;
    // Migrações históricas de envelope/paths vivem fora do serializer moderno.
    root = migration::normalizeHistoricalProjectPayload(
        root, ed.projectRoot(), !hadAssetDatabase && mode == ProjectLoadMode::Normal,
        &migratedProject);

    // Bloco B / 3.24.0: restaura o banco persistido e reconcilia os arquivos
    // do disco ANTES de desserializar o restante do projeto. Se um asset foi
    // movido/renomeado fora da LUDO, o hash preserva o GUID e assetReferences
    // permite trocar o caminho legado pelo caminho atual sem quebrar a UI,
    // eventos, mapas ou banco de dados.
    const QJsonObject persistedAssetDatabase = root.value(QStringLiteral("assetDatabase")).toObject();
    const QJsonArray persistedAssetReferences = root.value(QStringLiteral("assetReferences")).toArray();
    ed.assetDatabase.fromJson(persistedAssetDatabase);
    QString assetDatabaseError;
    if (!ed.assetDatabase.synchronize(ed.projectRoot(), &assetDatabaseError)) {
        if (error) *error = QObject::tr("Falha ao ler o Asset Database: %1").arg(assetDatabaseError);
        return false;
    }
    const QVector<AssetPathChange> assetPathChanges = ed.assetDatabase.takePathChanges();
    QHash<QString, QString> stableByLegacyPath;
    for (const QJsonValue& value : persistedAssetReferences) {
        const QJsonObject reference = value.toObject();
        const QString legacyPath = AssetDatabase::normalizePath(reference.value(QStringLiteral("path")).toString());
        const QString currentPath = ed.assetDatabase.pathForId(reference.value(QStringLiteral("id")).toString());
        if (!legacyPath.isEmpty() && !currentPath.isEmpty())
            stableByLegacyPath.insert(legacyPath.toLower(), currentPath);
    }
    root.remove(QStringLiteral("assetDatabase"));
    root.remove(QStringLiteral("assetReferences"));
    root = remapAssetStrings(root, ed.assetDatabase, stableByLegacyPath).toObject();

    // Estado de workspace é volátil e nunca deve atravessar a abertura de
    // outro projeto. A estrutura persistida continua sendo somente ed.docs.
    ed.session.resetWorkspace();
    ed.docs.clear();
    ed.tilesets.clear();
    ed.autotiles.clear();
    ed.wangSets.clear();
    ed.starTiles.clear();
    ed.randomPool.clear();
    ed.session.customStamp.clear();
    ed.session.tsSel = TilesetSelection();

    serialization::applyProjectIdentity(ed, root);

    const bool hasGlobalAutotiles = root.value("autotiles").isArray();
    QVector<TilesetAutotile> legacyAutotiles;

    for (const QJsonValue& v : root.value("tilesets").toArray()) {
        const QJsonObject t = v.toObject();
        Tileset ts;
        if (!t.value("id").toString().trimmed().isEmpty()) ts.id = t.value("id").toString();
        ts.name = t.value("name").toString();
        ts.category = t.value("category").toString();
        ts.internalAutotileAtlas = t.value("internalAutotileAtlas").toBool(false);
        ts.paletteVisible = t.value("paletteVisible").toBool(true);
        ts.pageGroupId = t.value("pageGroupId").toString().trimmed();
        ts.pageIndex = qMax(0, t.value("pageIndex").toInt(0));
        ts.generatedFromBake = t.value("generatedFromBake").toBool(false);
        ts.bakeSourceLayerId = t.value("bakeSourceLayerId").toString();
        ts.generatedFromSlope = t.value("generatedFromSlope").toBool(false);
        ts.slopeSourceLayerId = t.value("slopeSourceLayerId").toString();
        ts.slopeAxis = t.value("slopeAxis").toString();
        ts.slopeStep = t.value("slopeStep").toDouble(0.0);
        // `assetId` e opcional para manter ProjectFormat 2. Quando presente,
        // ele vence o caminho legado: o Asset Database ja foi sincronizado e
        // conhece o nome/local atual do arquivo. Projetos antigos continuam
        // usando `source` e o grafo assetReferences para a mesma migracao.
        const QString sourceAssetId = t.value("assetId").toString().trimmed();
        const QString sourceById = sourceAssetId.isEmpty()
            ? QString() : ed.assetDatabase.pathForId(sourceAssetId);
        ts.sourcePath = sourceById.isEmpty() ? t.value("source").toString() : sourceById;
        ts.sourceTileX = qMax(0, t.value("sourceTileX").toInt());
        ts.sourceTileY = qMax(0, t.value("sourceTileY").toInt());
        ts.image = dataUriToImage(t.value("src").toString());
        if (ts.image.isNull() && !ts.sourcePath.isEmpty())
            ts.image.load(QDir(ed.projectRoot()).filePath(ts.sourcePath));
        ts.tilewidth = qMax(1, t.value("tilewidth").toInt(32));
        ts.tileheight = qMax(1, t.value("tileheight").toInt(32));
        ts.spacing = t.value("spacing").toInt();
        ts.margin = t.value("margin").toInt();
        if (t.contains("chromaColor")) {
            ts.chromaApplied = true;
            ts.chromaColor = QColor(t.value("chromaColor").toString());
            ts.chromaTolerance = t.value("chromaTolerance").toInt();
        }
        ts.combined = t.value("combined").toBool(false);
        ts.combinedDirection = t.value("combinedDirection").toString(QStringLiteral("horizontal"));
        ts.combinedTileCount = t.value("combinedTileCount").toInt();
        for (const QJsonValue& sv : t.value("combinedSources").toArray()) {
            const QJsonObject so = sv.toObject();
            CombinedSource s;
            s.name = so.value("name").toString();
            s.sourcePath = so.value("source").toString();
            s.x = so.value("x").toInt(); s.y = so.value("y").toInt();
            s.cols = so.value("cols").toInt(); s.rows = so.value("rows").toInt();
            s.count = so.value("count").toInt();
            s.sourceTileX = qMax(0, so.value("sourceTileX").toInt());
            s.sourceTileY = qMax(0, so.value("sourceTileY").toInt());
            ts.combinedSources.push_back(s);
        }
        const QJsonObject cur = t.value("packCursor").toObject();
        ts.packCursorX = cur.value("x").toInt();
        ts.packCursorY = cur.value("y").toInt();
        if (!hasGlobalAutotiles) {
            for (const QJsonValue& av : t.value("autotiles").toArray()) {
                const QJsonObject ao = av.toObject();
                TilesetAutotile autotile;
                if (!ao.value("id").toString().trimmed().isEmpty()) autotile.id = ao.value("id").toString();
                autotile.name = ao.value("name").toString();
                autotile.category = ao.value("category").toString().trimmed();
                autotile.tilesetId = ts.id;
                autotile.baseX = ao.value("x").toInt(); autotile.baseY = ao.value("y").toInt();
                autotile.cols = ao.value("cols").toInt(); autotile.rows = ao.value("rows").toInt();
                autotile.wangSetId = ao.value("wangSetId").toString();
                autotile.wangColorId = ao.value("wangColorId").toInt(-1);
                autotile.animatedAutotileId = ao.value("animatedAutotileId").toString();
                autotile.previewTx = ao.value("previewX").toInt(autotile.baseX);
                autotile.previewTy = ao.value("previewY").toInt(autotile.baseY + qMax(0, autotile.rows - 1));
                autotile.extendAtMapBoundary = ao.contains("extendAtMapBoundary")
                    ? ao.value("extendAtMapBoundary").toBool(true) : true;
                autotile.resourceEffects = deserializeResourceEffects(ao.value("resourceEffects"));
                legacyAutotiles.push_back(autotile);
            }
        }
        for (const QJsonValue& av : t.value("animatedAutotiles").toArray()) {
            const QJsonObject ao = av.toObject();
            AnimatedAutotile anim;
            if (!ao.value("id").toString().trimmed().isEmpty()) anim.id = ao.value("id").toString();
            anim.name = ao.value("name").toString();
            anim.baseX = ao.value("x").toInt(); anim.baseY = ao.value("y").toInt();
            anim.cols = ao.value("cols").toInt(); anim.rows = ao.value("rows").toInt();
            anim.fps = ao.value("fps").toDouble(6.0);
            anim.loop = ao.contains("loop") ? ao.value("loop").toBool(true) : true;
            anim.pingPong = ao.value("pingPong").toBool(false);
            anim.synchronized = ao.contains("synchronized") ? ao.value("synchronized").toBool(true) : true;
            for (const QJsonValue& fv : ao.value("frames").toArray()) {
                const QJsonObject fo = fv.toObject();
                anim.frameOrigins.push_back(QPoint(fo.value("x").toInt(), fo.value("y").toInt()));
            }
            if (anim.frameOrigins.isEmpty() && anim.cols > 0 && anim.rows > 0)
                anim.frameOrigins.push_back(QPoint(anim.baseX, anim.baseY));
            ts.animatedAutotiles.push_back(anim);
        }
        ts.recomputeGrid();
        // Preserva colunas/linhas gravadas quando a imagem nao pode ser lida.
        if (ts.image.isNull()) {
            ts.imagewidth = t.value("imagewidth").toInt();
            ts.imageheight = t.value("imageheight").toInt();
            ts.columns = qMax(1, t.value("columns").toInt(1));
            ts.rows = qMax(1, t.value("rows").toInt(1));
            ts.tilecount = t.value("tilecount").toInt(ts.columns * ts.rows);
        }
        const QJsonObject priorities = t.value("tilePriorities").toObject();
        for (auto it = priorities.constBegin(); it != priorities.constEnd(); ++it) {
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() != 2 || !it.value().isDouble()) continue;
            const int tx = parts[0].toInt(), ty = parts[1].toInt();
            const int priority = clampi(it.value().toInt(), 0, 5);
            if (priority > 0 && ts.contains(tx, ty)) ts.setTilePriority(tx, ty, priority);
        }
        const QJsonObject collisionTiles = t.value("tileCollisionMasks").toObject();
        for (auto it = collisionTiles.constBegin(); it != collisionTiles.constEnd(); ++it) {
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() != 2) continue;
            const int tx = parts[0].toInt(), ty = parts[1].toInt();
            const int mask = it.value().toInt() & 0x0f;
            if (mask && ts.contains(tx, ty)) ts.setTileCollisionMask(tx, ty, mask);
        }
        const QJsonObject probabilities = t.value("tileProbabilities").toObject();
        for (auto it = probabilities.constBegin(); it != probabilities.constEnd(); ++it) {
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() != 2) continue;
            const int tx = parts[0].toInt(), ty = parts[1].toInt();
            const double probability = it.value().toDouble(1.0);
            if (ts.contains(tx, ty) && !qFuzzyCompare(probability, 1.0))
                ts.setTileProbability(tx, ty, probability);
        }
        const QJsonObject resourceBindings=t.value("tileResourceEffects").toObject();
        for(auto it=resourceBindings.constBegin();it!=resourceBindings.constEnd();++it) {
            const QStringList parts=it.key().split(QLatin1Char(':')); if(parts.size()!=2) continue;
            const int tx=parts[0].toInt(), ty=parts[1].toInt(); if(!ts.contains(tx,ty)) continue;
            const QVector<TileResourceEffectBinding> effects=deserializeResourceEffects(it.value()); if(!effects.isEmpty()) ts.tileResourceEffects.insert(it.key(),effects);
        }
        ed.tilesets.push_back(ts);
    }

    if (migration::migrateLegacyTilesetPages(ed)) migratedProject = true;

    if (hasGlobalAutotiles) {
        for (const QJsonValue& av : root.value("autotiles").toArray()) {
            const QJsonObject ao = av.toObject();
            TilesetAutotile autotile;
            if (!ao.value("id").toString().trimmed().isEmpty()) autotile.id = ao.value("id").toString();
            autotile.name = ao.value("name").toString();
            autotile.category = ao.value("category").toString().trimmed();
            autotile.tilesetId = ao.value("tilesetId").toString();
            autotile.baseX = ao.value("x").toInt(); autotile.baseY = ao.value("y").toInt();
            autotile.cols = ao.value("cols").toInt(); autotile.rows = ao.value("rows").toInt();
            autotile.wangSetId = ao.value("wangSetId").toString();
            autotile.wangColorId = ao.value("wangColorId").toInt(-1);
            autotile.animatedAutotileId = ao.value("animatedAutotileId").toString();
            autotile.previewTx = ao.value("previewX").toInt(autotile.baseX);
            autotile.previewTy = ao.value("previewY").toInt(autotile.baseY + qMax(0, autotile.rows - 1));
            autotile.extendAtMapBoundary = ao.contains("extendAtMapBoundary")
                ? ao.value("extendAtMapBoundary").toBool(true) : true;
            autotile.resourceEffects = deserializeResourceEffects(ao.value("resourceEffects"));
            ed.autotiles.push_back(autotile);
        }
    } else {
        ed.autotiles = legacyAutotiles;
        if (!legacyAutotiles.isEmpty()) migratedProject = true;
    }

    ed.reindexTilesetGids();
    const QVector<int> visibleTilesets = paletteTilesetIndices(ed);
    ed.session.activeTilesetIdx = visibleTilesets.isEmpty() ? -1 : visibleTilesets.first();
    if (ed.session.activeTilesetIdx >= 0)
        ed.session.tsSel = TilesetSelection{ ed.session.activeTilesetIdx, 0, 0, 1, 1 };

    for (const QJsonValue& v : root.value("wangSets").toArray()) {
        const QJsonObject w = v.toObject();
        WangSet ws;
        ws.id = w.value("id").toString(idGen());
        ws.name = w.value("name").toString();
        ws.type = w.value("type").toString(QStringLiteral("mixed"));
        if (w.contains("icon")) {
            const QJsonObject ic = w.value("icon").toObject();
            ws.hasIcon = true;
            ws.iconTilesetIdx = ic.value("tilesetIdx").toInt(-1);
            ws.iconTx = ic.value("tx").toInt();
            ws.iconTy = ic.value("ty").toInt();
        }
        for (const QJsonValue& cv : w.value("colors").toArray()) {
            const QJsonObject c = cv.toObject();
            WangColor wc;
            wc.id = c.value("id").toInt(1);
            wc.name = c.value("name").toString();
            wc.color = QColor(c.value("color").toString(QStringLiteral("#4a90d7")));
            if (c.contains("icon")) {
                const QJsonObject ic = c.value("icon").toObject();
                wc.hasIcon = true;
                wc.iconTilesetIdx = ic.value("tilesetIdx").toInt(-1);
                wc.iconTx = ic.value("tx").toInt();
                wc.iconTy = ic.value("ty").toInt();
            }
            ws.colors.push_back(wc);
        }
        const QJsonObject tiles = w.value("tiles").toObject();
        for (auto it = tiles.constBegin(); it != tiles.constEnd(); ++it) {
            const QJsonObject d = it.value().toObject();
            WangTileData wd;
            for (const QString& pos : WangTileData::positions()) {
                if (!d.contains(pos)) continue;
                const int cid = d.value(pos).toInt(-1);
                wd.set(pos, cid > 0 ? cid : -1);   // a versao web grava 0 = sem cor
            }
            if (d.contains("_isolatedColorId"))
                wd.isolatedColorId = d.value("_isolatedColorId").toInt(-1);
            ws.tiles.insert(it.key(), wd);
        }
        ed.wangSets.push_back(ws);
    }
    ed.session.activeWangSetIdx = ed.wangSets.isEmpty() ? -1 : 0;
    if (const WangSet* ws = ed.activeWangSet())
        if (!ws->colors.isEmpty()) ed.session.activeWangColorId = ws->colors.first().id;

    // Presets Wang persistidos continuam disponíveis ANTES da reconciliação
    // para autoria manual e para migrar a revisão imediatamente anterior, que
    // podia ter vinculado 12x4 a um "Terrenos (12x4)" do QSettings. Novas
    // importações 12x4 usam a geometria canônica embutida no engine.
    loadWangPresetsFromSettings(ed);

    // Tilesets antigos nao tinham catalogo de Autotiles. A identidade 2.0 e
    // sintetizada a partir do Wang/AnimatedAutotile canonico e sera persistida
    // no proximo save, sem mudar ProjectFormat.
    if (reconcileTilesetAutotiles(ed) > 0) migratedProject = true;

    migration::migrateLegacyTileMetadata(ed, root);

    for (const QJsonValue& v : root.value("randomPool").toArray()) {
        const QJsonObject o = v.toObject();
        RandomEntry e;
        e.tilesetIdx = o.value("tilesetIdx").toInt(-1);
        e.x = o.value("x").toInt(); e.y = o.value("y").toInt();
        e.w = qMax(1, o.value("w").toInt(1)); e.h = qMax(1, o.value("h").toInt(1));
        const QJsonArray tiles = o.value("tiles").toArray();
        for (const QJsonValue& tv : tiles) {
            const QJsonObject t = tv.toObject();
            e.tiles.push_back(tileRefFromJson(t));
            e.offsets.push_back(QPoint(t.value("dx").toInt(), t.value("dy").toInt()));
        }
        if (e.tiles.isEmpty()) {
            for (int dy = 0; dy < e.h; ++dy)
                for (int dx = 0; dx < e.w; ++dx) {
                    TileRef t; t.tilesetIdx = e.tilesetIdx; t.tx = e.x + dx; t.ty = e.y + dy;
                    e.tiles.push_back(t);
                    e.offsets.push_back(QPoint(dx, dy));
                }
        }
        if (e.tilesetIdx >= 0) ed.randomPool.push_back(e);
    }

    ed.savedStamps.clear();
    for (const QJsonValue& v : root.value("patterns").toArray()) {
        const QJsonObject o = v.toObject();
        SavedStamp saved;
        if (!o.value("id").toString().trimmed().isEmpty()) saved.id = o.value("id").toString();
        saved.name = o.value("name").toString(QObject::tr("Pattern"));
        saved.stamp.w = qMax(1, o.value("w").toInt(1));
        saved.stamp.h = qMax(1, o.value("h").toInt(1));
        for (const QJsonValue& tv : o.value("tiles").toArray()) {
            const QJsonObject t = tv.toObject();
            const TileRef ref = tileRefFromJson(t);
            if (!ref.isValid()) continue;
            saved.stamp.tiles.push_back(ref);
            saved.stamp.offsets.push_back(QPoint(t.value("dx").toInt(), t.value("dy").toInt()));
        }
        if (saved.stamp.valid()) ed.savedStamps.push_back(saved);
    }

    const QJsonArray maps = root.value("maps").toArray();
    if (!maps.isEmpty()) {
        for (const QJsonValue& value : maps) {
            const QJsonObject mapObject = value.toObject();
            MapDoc map;
            QString mapError;
            if (!loadProjectMapPayload(ed, mapObject, &map, &mapError)) {
                if (error) *error = mapError;
                return false;
            }
            ed.docs.push_back(map);
        }
    } else {
        MapDoc map;
        map.name = QStringLiteral("Mapa 1");
        map.map = deserializeProjectMapInfo(ed, root.value("map").toObject());
        map.activeLayerIdx = 0;
        for (const QJsonValue& layerValue : root.value("layers").toArray())
            map.layers.push_back(deserializeNode(layerValue.toObject()));
        ed.docs.push_back(map);
        migratedProject = true;
    }

    migration::finalizeLoadedHistoricalState(ed, jdoc, &migratedProject);
    ed.activeDocIdx = clampi(root.value("activeMapDocIdx").toInt(0), 0, ed.docs.size() - 1);
    ed.session.selectedLayerId = ed.activeLayer() ? ed.activeLayer()->id : QString();
    ed.projectPath = path;
    // Projetos sem Asset Database e projetos que tiveram paths reconciliados
    // ficam dirty uma única vez para persistir GUIDs/aliases no próximo Save.
    ed.projectDirty = migratedProject || !hadAssetDatabase || !assetPathChanges.isEmpty();

    emit ed.docsChanged();
    emit ed.tilesetsChanged();
    emit ed.wangChanged();
    emit ed.layersChanged();
    emit ed.mapChanged();
    emit ed.projectChanged();
    emit ed.historyChanged();
    ed.resources().notifyAssetsChanged();
    return true;
}


}} // namespace core::io
