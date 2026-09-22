#include "CommonEventPackage.h"

#include "CustomDatabase.h"
#include "EventCommandCodec.h"
#include "Database.h"
#include "Editor.h"
#include "NoCodePlugin.h"
#include "ProjectReferenceIndex.h"
#include "Version.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace core {
namespace {

constexpr qint64 kMaxPackageBytes = 192ll * 1024ll * 1024ll;
constexpr qint64 kMaxEmbeddedAssetBytes = 32ll * 1024ll * 1024ll;
constexpr qint64 kMaxTotalAssetBytes = 128ll * 1024ll * 1024ll;
constexpr int kMaxPortableCommonEvents = 4096;
constexpr int kMaxPortableDependencyItems = 32768;
constexpr int kMaxPortableAssets = 4096;
constexpr int kMaxCommandsPerCommonEvent = 100000;
constexpr int kMaxFieldsPerCustomDatabase = 4096;
constexpr int kMaxRecordsPerCustomDatabase = 100000;

struct EmbeddedAsset {
    QString path;
    QByteArray bytes;
    QByteArray sha256;
};

bool isSafeAssetPath(const QString& raw)
{
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(raw.trimmed()));
    if (path.isEmpty() || QDir::isAbsolutePath(path)) return false;
    if (path == QLatin1String("Assets")) return true;
    return path.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)
        && !path.contains(QStringLiteral("/../")) && !path.endsWith(QStringLiteral("/.."));
}

QJsonObject commonEventToJson(const CommonEvent& c)
{
    QJsonObject o;
    o[QStringLiteral("id")] = c.id; o[QStringLiteral("number")] = c.number;
    o[QStringLiteral("name")] = c.name; o[QStringLiteral("category")] = c.category;
    o[QStringLiteral("description")] = c.description;
    o[QStringLiteral("trigger")] = commonTriggerId(c.trigger); o[QStringLiteral("switchId")] = c.switchId;
    o[QStringLiteral("advancedTrigger")] = c.advancedTrigger;
    o[QStringLiteral("triggerValueType")] = commonValueTypeId(c.triggerValueType);
    o[QStringLiteral("triggerLeft")] = QJsonObject::fromVariantMap(c.triggerLeft);
    o[QStringLiteral("triggerOp")] = c.triggerOp;
    o[QStringLiteral("triggerRight")] = QJsonObject::fromVariantMap(c.triggerRight);
    o[QStringLiteral("schedulePolicy")] = commonSchedulePolicyId(c.schedulePolicy);
    o[QStringLiteral("intervalFrames")] = c.intervalFrames; o[QStringLiteral("priority")] = c.priority;
    QJsonArray parameters;
    for (const CommonEventParameter& p : c.parameters) {
        parameters.append(QJsonObject{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name},
            {QStringLiteral("type"), commonValueTypeId(p.type)},
            {QStringLiteral("default"), QJsonValue::fromVariant(normalizeCommonValue(p.defaultValue, p.type))},
            {QStringLiteral("required"), p.required}, {QStringLiteral("description"), p.description}});
    }
    o[QStringLiteral("parameters")] = parameters;
    QJsonArray locals;
    for (const CommonEventLocal& l : c.locals) {
        locals.append(QJsonObject{{QStringLiteral("id"), l.id}, {QStringLiteral("name"), l.name},
            {QStringLiteral("type"), commonValueTypeId(l.type)},
            {QStringLiteral("initial"), QJsonValue::fromVariant(normalizeCommonValue(l.initialValue, l.type))}});
    }
    o[QStringLiteral("locals")] = locals;
    o[QStringLiteral("return")] = QJsonObject{{QStringLiteral("enabled"), c.returnValue.enabled},
        {QStringLiteral("name"), c.returnValue.name}, {QStringLiteral("type"), commonValueTypeId(c.returnValue.type)},
        {QStringLiteral("default"), QJsonValue::fromVariant(normalizeCommonValue(c.returnValue.defaultValue, c.returnValue.type))}};
    o[QStringLiteral("commands")] = eventCommandsToJson(c.commands);
    return o;
}

CommonEvent commonEventFromJson(const QJsonObject& o)
{
    CommonEvent c;
    c.id = o.value(QStringLiteral("id")).toString(idGen());
    c.number = qMax(1, o.value(QStringLiteral("number")).toInt(1));
    c.name = o.value(QStringLiteral("name")).toString().left(128);
    c.category = o.value(QStringLiteral("category")).toString().left(128);
    c.description = o.value(QStringLiteral("description")).toString().left(4096);
    c.trigger = commonTriggerFromId(o.value(QStringLiteral("trigger")).toString());
    c.switchId = qMax(0, o.value(QStringLiteral("switchId")).toInt(0));
    c.advancedTrigger = o.value(QStringLiteral("advancedTrigger")).toBool(false);
    c.triggerValueType = commonValueTypeFromId(o.value(QStringLiteral("triggerValueType")).toString());
    c.triggerLeft = o.value(QStringLiteral("triggerLeft")).toObject().toVariantMap();
    c.triggerOp = o.value(QStringLiteral("triggerOp")).toString(QStringLiteral("=="));
    c.triggerRight = o.value(QStringLiteral("triggerRight")).toObject().toVariantMap();
    c.schedulePolicy = commonSchedulePolicyFromId(o.value(QStringLiteral("schedulePolicy")).toString());
    c.intervalFrames = qBound(1, o.value(QStringLiteral("intervalFrames")).toInt(60), 360000);
    c.priority = qBound(-1000, o.value(QStringLiteral("priority")).toInt(0), 1000);
    for (const QJsonValue& value : o.value(QStringLiteral("parameters")).toArray()) {
        const QJsonObject p = value.toObject(); CommonEventParameter def;
        def.id = p.value(QStringLiteral("id")).toString(idGen()); def.name = p.value(QStringLiteral("name")).toString().left(128);
        def.type = commonValueTypeFromId(p.value(QStringLiteral("type")).toString());
        def.defaultValue = normalizeCommonValue(p.value(QStringLiteral("default")).toVariant(), def.type);
        def.required = p.value(QStringLiteral("required")).toBool(false);
        def.description = p.value(QStringLiteral("description")).toString().left(1024); c.parameters.push_back(def);
    }
    for (const QJsonValue& value : o.value(QStringLiteral("locals")).toArray()) {
        const QJsonObject l = value.toObject(); CommonEventLocal def;
        def.id = l.value(QStringLiteral("id")).toString(idGen()); def.name = l.value(QStringLiteral("name")).toString().left(128);
        def.type = commonValueTypeFromId(l.value(QStringLiteral("type")).toString());
        def.initialValue = normalizeCommonValue(l.value(QStringLiteral("initial")).toVariant(), def.type); c.locals.push_back(def);
    }
    const QJsonObject r = o.value(QStringLiteral("return")).toObject();
    c.returnValue.enabled = r.value(QStringLiteral("enabled")).toBool(false);
    c.returnValue.name = r.value(QStringLiteral("name")).toString(QStringLiteral("Resultado")).left(128);
    c.returnValue.type = commonValueTypeFromId(r.value(QStringLiteral("type")).toString());
    c.returnValue.defaultValue = normalizeCommonValue(r.value(QStringLiteral("default")).toVariant(), c.returnValue.type);
    c.commands = eventCommandsFromJson(o.value(QStringLiteral("commands")).toArray(), kMaxCommandsPerCommonEvent);
    return c;
}

QJsonObject customDatabaseToJson(const CustomDatabaseDefinition& db)
{
    QJsonObject o{{QStringLiteral("id"), db.id}, {QStringLiteral("number"), db.number},
                  {QStringLiteral("name"), db.name}, {QStringLiteral("description"), db.description},
                  {QStringLiteral("mode"), customDatabaseModeId(db.mode)}};
    QJsonArray fields;
    for (const CustomDatabaseField& field : db.fields)
        fields.append(QJsonObject{{QStringLiteral("id"), field.id}, {QStringLiteral("name"), field.name},
            {QStringLiteral("type"), customDatabaseFieldTypeId(field.type)},
            {QStringLiteral("default"), QJsonValue::fromVariant(field.defaultValue)},
            {QStringLiteral("referenceDatabaseId"), field.referenceDatabaseId}});
    QJsonArray records;
    for (const CustomDatabaseRecord& record : db.records)
        records.append(QJsonObject{{QStringLiteral("id"), record.id}, {QStringLiteral("number"), record.number},
            {QStringLiteral("name"), record.name}, {QStringLiteral("description"), record.description},
            {QStringLiteral("values"), QJsonObject::fromVariantMap(record.values)}});
    o[QStringLiteral("fields")] = fields; o[QStringLiteral("records")] = records;
    return o;
}

CustomDatabaseDefinition customDatabaseFromJson(const QJsonObject& o)
{
    CustomDatabaseDefinition db;
    db.id = o.value(QStringLiteral("id")).toString(idGen()); db.number = qMax(1, o.value(QStringLiteral("number")).toInt(1));
    db.name = o.value(QStringLiteral("name")).toString().left(128); db.description = o.value(QStringLiteral("description")).toString().left(4096);
    db.mode = customDatabaseModeFromId(o.value(QStringLiteral("mode")).toString());
    for (const QJsonValue& value : o.value(QStringLiteral("fields")).toArray()) {
        const QJsonObject f = value.toObject(); CustomDatabaseField field;
        field.id = f.value(QStringLiteral("id")).toString(idGen()); field.name = f.value(QStringLiteral("name")).toString().left(128);
        field.type = customDatabaseFieldTypeFromId(f.value(QStringLiteral("type")).toString());
        field.defaultValue = f.value(QStringLiteral("default")).toVariant();
        field.referenceDatabaseId = f.value(QStringLiteral("referenceDatabaseId")).toString(); db.fields.push_back(field);
    }
    for (const QJsonValue& value : o.value(QStringLiteral("records")).toArray()) {
        const QJsonObject r = value.toObject(); CustomDatabaseRecord record;
        record.id = r.value(QStringLiteral("id")).toString(idGen()); record.number = qMax(1, r.value(QStringLiteral("number")).toInt(1));
        record.name = r.value(QStringLiteral("name")).toString().left(128); record.description = r.value(QStringLiteral("description")).toString().left(4096);
        record.values = r.value(QStringLiteral("values")).toObject().toVariantMap(); db.records.push_back(record);
    }
    normalizeCustomDatabaseDefinition(db); return db;
}

QJsonObject footstepSurfaceToJson(const Editor& ed, const FootstepSurface& surface)
{
    QJsonObject out;
    out[QStringLiteral("id")] = surface.id;
    out[QStringLiteral("name")] = surface.name;
    out[QStringLiteral("volume")] = qBound(0, surface.volume, 100);
    out[QStringLiteral("pitchMin")] = qBound(50, surface.pitchMin, 200);
    out[QStringLiteral("pitchMax")] = qBound(50, surface.pitchMax, 200);
    out[QStringLiteral("avoidImmediateRepeat")] = surface.avoidImmediateRepeat;
    QJsonArray sounds;
    for (const FootstepSound& sound : surface.sounds) {
        QString source;
        if (!sound.assetId.trimmed().isEmpty()) source = ed.assetDatabase.pathForId(sound.assetId.trimmed());
        if (source.isEmpty()) source = sound.sourcePath.trimmed();
        QJsonObject item;
        if (!source.isEmpty()) item[QStringLiteral("source")] = source;
        item[QStringLiteral("weight")] = qMax(1, sound.weight);
        sounds.append(item);
    }
    out[QStringLiteral("sounds")] = sounds;
    return out;
}

FootstepSurface footstepSurfaceFromJson(const QJsonObject& object)
{
    FootstepSurface surface;
    surface.id = object.value(QStringLiteral("id")).toString(idGen());
    surface.name = object.value(QStringLiteral("name")).toString(QStringLiteral("Superfície")).left(128);
    surface.volume = qBound(0, object.value(QStringLiteral("volume")).toInt(80), 100);
    const int a = qBound(50, object.value(QStringLiteral("pitchMin")).toInt(95), 200);
    const int b = qBound(50, object.value(QStringLiteral("pitchMax")).toInt(105), 200);
    surface.pitchMin = qMin(a, b); surface.pitchMax = qMax(a, b);
    surface.avoidImmediateRepeat = object.contains(QStringLiteral("avoidImmediateRepeat"))
        ? object.value(QStringLiteral("avoidImmediateRepeat")).toBool(true) : true;
    for (const QJsonValue& value : object.value(QStringLiteral("sounds")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject(); FootstepSound sound;
        // Asset IDs são locais ao projeto. O pacote leva o caminho portátil e,
        // após a importação, recebe o GUID do Asset Database de destino.
        sound.sourcePath = item.value(QStringLiteral("source")).toString().trimmed();
        sound.weight = qMax(1, item.value(QStringLiteral("weight")).toInt(1));
        surface.sounds.push_back(sound);
    }
    return surface;
}

struct Dependencies {
    QSet<QString> commonIds; QSet<int> switches; QSet<int> variables; QSet<int> strings;
    QSet<QString> customDbIds; QSet<QString> pluginIds; QSet<QString> mapIds;
    QSet<QString> rpgRecords; QSet<QString> tilesetIds; QSet<QString> footstepSurfaceIds;
    QSet<QString> scannedCommonIds;
};

void collectVariantDependencies(const QVariant& value, Dependencies& deps)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap(); const QString kind = map.value(QStringLiteral("kind")).toString();
        const QString targetKind = map.value(QStringLiteral("target")).toString();
        if(targetKind==QLatin1String("switch"))deps.switches.insert(map.value(QStringLiteral("id")).toInt());
        else if(targetKind==QLatin1String("variable"))deps.variables.insert(map.value(QStringLiteral("id")).toInt());
        else if(targetKind==QLatin1String("string"))deps.strings.insert(map.value(QStringLiteral("id")).toInt());
        if (kind == QLatin1String("switch")) deps.switches.insert(map.value(QStringLiteral("id")).toInt());
        else if (kind == QLatin1String("variable")) deps.variables.insert(map.value(QStringLiteral("id")).toInt());
        else if (kind == QLatin1String("string")) deps.strings.insert(map.value(QStringLiteral("id")).toInt());
        if (map.contains(QStringLiteral("switchId"))) deps.switches.insert(map.value(QStringLiteral("switchId")).toInt());
        if (map.contains(QStringLiteral("variableId"))) deps.variables.insert(map.value(QStringLiteral("variableId")).toInt());
        if (map.contains(QStringLiteral("stringId"))) deps.strings.insert(map.value(QStringLiteral("stringId")).toInt());
        if (map.contains(QStringLiteral("databaseId"))) deps.customDbIds.insert(map.value(QStringLiteral("databaseId")).toString());
        if (map.contains(QStringLiteral("referenceDatabaseId"))) deps.customDbIds.insert(map.value(QStringLiteral("referenceDatabaseId")).toString());
        if (map.contains(QStringLiteral("pluginId"))) deps.pluginIds.insert(map.value(QStringLiteral("pluginId")).toString());
        if (map.contains(QStringLiteral("mapId"))) deps.mapIds.insert(map.value(QStringLiteral("mapId")).toString());
        if (map.contains(QStringLiteral("tilesetId"))) deps.tilesetIds.insert(map.value(QStringLiteral("tilesetId")).toString());
        if (map.contains(QStringLiteral("surfaceId"))) deps.footstepSurfaceIds.insert(map.value(QStringLiteral("surfaceId")).toString());
        const struct { const char* key; const char* category; } dbKeys[] = {
            {"actorId","actors"},{"classId","classes"},{"skillId","skills"},{"itemId","items"},
            {"weaponId","weapons"},{"armorId","armors"},{"stateId","states"},{"enemyId","enemies"},
            {"troopId","troops"},{"animationId","animations"},{"questId","quests"}
        };
        for(const auto& entry:dbKeys){const QString key=QString::fromLatin1(entry.key);if(map.contains(key)){const QString id=map.value(key).toString();if(!id.isEmpty())deps.rpgRecords.insert(QString::fromLatin1(entry.category)+QLatin1Char('|')+id);}}
        if (map.contains(QStringLiteral("commonId"))) deps.commonIds.insert(map.value(QStringLiteral("commonId")).toString());
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) collectVariantDependencies(it.value(), deps);
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant& child : value.toList()) collectVariantDependencies(child, deps);
    }
}

void collectCommonRecursive(const Editor& ed, const CommonEvent& common, Dependencies& deps);

void collectCommandDependencies(const Editor& ed, const EventCommand& command, Dependencies& deps)
{
    if(command.type==QLatin1String("switch.set"))deps.switches.insert(command.params.value(QStringLiteral("id")).toInt());
    else if(command.type==QLatin1String("string.set"))deps.strings.insert(command.params.value(QStringLiteral("id")).toInt());
    else if(command.type==QLatin1String("variable.set")||command.type==QLatin1String("variable.math")){
        const int first=command.params.value(QStringLiteral("id")).toInt();
        const int last=command.params.value(QStringLiteral("rangeEndId"),first).toInt();
        const int lo=qMin(first,last),hi=qMax(first,last);
        for(const VariableDef& variable:ed.variables)if(variable.id>=lo&&variable.id<=hi)deps.variables.insert(variable.id);
    }
    collectVariantDependencies(command.params, deps);
    if (command.type == QLatin1String("common.call") || command.type == QLatin1String("common.reserve")) {
        const QString stable = command.params.value(QStringLiteral("commonId")).toString();
        const CommonEvent* target = stable.isEmpty() ? ed.commonEventByNumber(command.params.value(QStringLiteral("number")).toInt())
                                                     : ed.commonEventById(stable);
        if (target) collectCommonRecursive(ed, *target, deps);
    }
}

void collectCommonRecursive(const Editor& ed, const CommonEvent& common, Dependencies& deps)
{
    if (deps.scannedCommonIds.contains(common.id)) return;
    deps.scannedCommonIds.insert(common.id); deps.commonIds.insert(common.id);
    if (!common.advancedTrigger && common.switchId > 0) deps.switches.insert(common.switchId);
    collectVariantDependencies(common.triggerLeft, deps); collectVariantDependencies(common.triggerRight, deps);
    for (const EventCommand& command : common.commands) collectCommandDependencies(ed, command, deps);
}

void collectJsonAssetPaths(const QJsonValue& value, QSet<QString>& out)
{
    if (value.isString()) {
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(value.toString()).trimmed());
        if (isSafeAssetPath(path) && path != QLatin1String("Assets")) out.insert(path);
    } else if (value.isArray()) {
        for (const QJsonValue& child : value.toArray()) collectJsonAssetPaths(child, out);
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) collectJsonAssetPaths(it.value(), out);
    }
}

void rewriteVariant(QVariant& value, const QHash<int,int>& switchMap, const QHash<int,int>& variableMap,
                    const QHash<int,int>& stringMap, const QHash<QString,QString>& commonMap,
                    const QHash<int,int>& commonNumberMap, const QHash<QString,QString>& dbMap,
                    const QHash<QString,QString>& fieldMap, const QHash<QString,QString>& recordMap,
                    const QHash<QString,QString>& mapMap, const QHash<QString,QString>& assetMap)
{
    if (value.metaType().id() == QMetaType::QString) {
        const QString current = QDir::fromNativeSeparators(value.toString());
        if (assetMap.contains(current)) value = assetMap.value(current);
        return;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList list = value.toList(); for (QVariant& child : list) rewriteVariant(child, switchMap, variableMap, stringMap, commonMap, commonNumberMap, dbMap, fieldMap, recordMap, mapMap, assetMap); value = list; return;
    }
    if (value.metaType().id() != QMetaType::QVariantMap) return;
    QVariantMap map = value.toMap(); const QString kind = map.value(QStringLiteral("kind")).toString();
    const QString targetKind=map.value(QStringLiteral("target")).toString();
    if(targetKind==QLatin1String("switch")){const int old=map.value(QStringLiteral("id")).toInt();map[QStringLiteral("id")]=switchMap.value(old,old);}
    else if(targetKind==QLatin1String("variable")){const int old=map.value(QStringLiteral("id")).toInt();map[QStringLiteral("id")]=variableMap.value(old,old);}
    else if(targetKind==QLatin1String("string")){const int old=map.value(QStringLiteral("id")).toInt();map[QStringLiteral("id")]=stringMap.value(old,old);}
    if (kind == QLatin1String("switch")) map[QStringLiteral("id")] = switchMap.value(map.value(QStringLiteral("id")).toInt(), map.value(QStringLiteral("id")).toInt());
    else if (kind == QLatin1String("variable")) map[QStringLiteral("id")] = variableMap.value(map.value(QStringLiteral("id")).toInt(), map.value(QStringLiteral("id")).toInt());
    else if (kind == QLatin1String("string")) map[QStringLiteral("id")] = stringMap.value(map.value(QStringLiteral("id")).toInt(), map.value(QStringLiteral("id")).toInt());
    if (map.contains(QStringLiteral("switchId"))) { const int old=map.value(QStringLiteral("switchId")).toInt(); map[QStringLiteral("switchId")] = switchMap.value(old,old); }
    if (map.contains(QStringLiteral("variableId"))) { const int old=map.value(QStringLiteral("variableId")).toInt(); map[QStringLiteral("variableId")] = variableMap.value(old,old); }
    if (map.contains(QStringLiteral("stringId"))) { const int old=map.value(QStringLiteral("stringId")).toInt(); map[QStringLiteral("stringId")] = stringMap.value(old,old); }
    if (map.contains(QStringLiteral("commonId"))) { const QString old=map.value(QStringLiteral("commonId")).toString(); map[QStringLiteral("commonId")] = commonMap.value(old,old); }
    if (map.contains(QStringLiteral("databaseId"))) { const QString old=map.value(QStringLiteral("databaseId")).toString(); map[QStringLiteral("databaseId")] = dbMap.value(old,old); }
    if (map.contains(QStringLiteral("referenceDatabaseId"))) { const QString old=map.value(QStringLiteral("referenceDatabaseId")).toString(); map[QStringLiteral("referenceDatabaseId")] = dbMap.value(old,old); }
    if (map.contains(QStringLiteral("fieldId"))) { const QString old=map.value(QStringLiteral("fieldId")).toString(); map[QStringLiteral("fieldId")] = fieldMap.value(old,old); }
    if (map.contains(QStringLiteral("recordId"))) { const QString old=map.value(QStringLiteral("recordId")).toString(); map[QStringLiteral("recordId")] = recordMap.value(old,old); }
    if (map.contains(QStringLiteral("mapId"))) { const QString old=map.value(QStringLiteral("mapId")).toString(); map[QStringLiteral("mapId")] = mapMap.value(old,old); }
    for (auto it=map.begin(); it!=map.end(); ++it) { QVariant child=it.value(); rewriteVariant(child,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap); it.value()=child; }
    value = map;
}

void rewriteExternalProjectRefs(QVariant& value, const QHash<QString,QString>& rpgMap,
                                const QHash<QString,QString>& tilesetMap)
{
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant& child : list) rewriteExternalProjectRefs(child, rpgMap, tilesetMap);
        value = list;
        return;
    }
    if (value.metaType().id() != QMetaType::QVariantMap) return;
    QVariantMap map = value.toMap();
    if (map.contains(QStringLiteral("tilesetId"))) {
        const QString old = map.value(QStringLiteral("tilesetId")).toString();
        map[QStringLiteral("tilesetId")] = tilesetMap.value(old, old);
    }
    const struct { const char* key; const char* category; } dbKeys[] = {
        {"actorId","actors"},{"classId","classes"},{"skillId","skills"},{"itemId","items"},
        {"weaponId","weapons"},{"armorId","armors"},{"stateId","states"},{"enemyId","enemies"},
        {"troopId","troops"},{"animationId","animations"},{"questId","quests"}
    };
    for (const auto& entry : dbKeys) {
        const QString key = QString::fromLatin1(entry.key);
        if (!map.contains(key)) continue;
        const QString old = map.value(key).toString();
        const QString token = QString::fromLatin1(entry.category) + QLatin1Char('|') + old;
        map[key] = rpgMap.value(token, old);
    }
    for (auto it = map.begin(); it != map.end(); ++it) {
        QVariant child = it.value();
        rewriteExternalProjectRefs(child, rpgMap, tilesetMap);
        it.value() = child;
    }
    value = map;
}

void rewriteFootstepRefs(QVariant& value, const QHash<QString,QString>& footstepMap)
{
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant& child : list) rewriteFootstepRefs(child, footstepMap);
        value = list; return;
    }
    if (value.metaType().id() != QMetaType::QVariantMap) return;
    QVariantMap map = value.toMap();
    if (map.contains(QStringLiteral("surfaceId"))) {
        const QString old = map.value(QStringLiteral("surfaceId")).toString();
        map[QStringLiteral("surfaceId")] = footstepMap.value(old, old);
    }
    for (auto it = map.begin(); it != map.end(); ++it) {
        QVariant child = it.value(); rewriteFootstepRefs(child, footstepMap); it.value() = child;
    }
    value = map;
}

void rewriteCommand(EventCommand& command, const QHash<int,int>& switchMap, const QHash<int,int>& variableMap,
                    const QHash<int,int>& stringMap, const QHash<QString,QString>& commonMap,
                    const QHash<int,int>& commonNumberMap, const QHash<QString,QString>& dbMap,
                    const QHash<QString,QString>& fieldMap, const QHash<QString,QString>& recordMap,
                    const QHash<QString,QString>& mapMap, const QHash<QString,QString>& assetMap)
{
    QVariant params = command.params; rewriteVariant(params,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap); command.params=params.toMap();
    if(command.type==QLatin1String("switch.set")){const int old=command.params.value(QStringLiteral("id")).toInt();command.params[QStringLiteral("id")]=switchMap.value(old,old);}
    else if(command.type==QLatin1String("string.set")){const int old=command.params.value(QStringLiteral("id")).toInt();command.params[QStringLiteral("id")]=stringMap.value(old,old);}
    else if(command.type==QLatin1String("variable.set")||command.type==QLatin1String("variable.math")){const int old=command.params.value(QStringLiteral("id")).toInt();command.params[QStringLiteral("id")]=variableMap.value(old,old);if(command.params.contains(QStringLiteral("rangeEndId"))){const int end=command.params.value(QStringLiteral("rangeEndId")).toInt();command.params[QStringLiteral("rangeEndId")]=variableMap.value(end,end);}}
    if (command.type == QLatin1String("common.call") || command.type == QLatin1String("common.reserve")) {
        const QString oldId = command.params.value(QStringLiteral("commonId")).toString();
        if (!oldId.isEmpty()) command.params[QStringLiteral("commonId")] = commonMap.value(oldId, oldId);
        const int oldNumber = command.params.value(QStringLiteral("number")).toInt();
        if (commonNumberMap.contains(oldNumber)) command.params[QStringLiteral("number")] = commonNumberMap.value(oldNumber);
    }
}

int nextFreeInt(const QSet<int>& used, int start = 1) { int id=qMax(1,start); while(used.contains(id)) ++id; return id; }
int nextFreeCommonNumber(const Editor& ed, QSet<int>& used) { for(const CommonEvent& c:ed.commonEvents)used.insert(c.number); return nextFreeInt(used); }

QString safeImportedAssetPath(const QString& rootToken, const QString& oldPath,
                              const QByteArray& contentHash = QByteArray())
{
    QString tail = QDir::cleanPath(QDir::fromNativeSeparators(oldPath));
    if (tail.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) tail = tail.mid(7);
    while (tail.startsWith(QStringLiteral("../"))) tail = tail.mid(3);
    tail.replace(QLatin1Char(':'), QLatin1Char('_'));
    if (tail.isEmpty() || tail == QLatin1String(".")) tail = QStringLiteral("asset.bin");
    const QByteArray seed=rootToken.toUtf8()+QByteArrayLiteral("|")+contentHash;
    const QByteArray digest=QCryptographicHash::hash(seed,QCryptographicHash::Sha256).toHex();
    const QString token = rootToken.isEmpty() ? QStringLiteral("import") : QString::fromLatin1(digest.left(20));
    return QStringLiteral("Assets/Imported/Common/%1/%2").arg(token, tail);
}

} // namespace

bool exportCommonEventPackage(const Editor& ed, const QString& commonEventId,
                              const QString& path, QString* error, CommonEventPackageSummary* summary)
{
    const CommonEvent* rootEvent = ed.commonEventById(commonEventId);
    if (!rootEvent) { if(error)*error=QCoreApplication::translate("CommonEventPackage","O Evento Comum selecionado não existe."); return false; }
    Dependencies deps; collectCommonRecursive(ed,*rootEvent,deps);
    QSet<QString> scannedPlugins;
    while (true) {
        const NoCodePlugin* next = nullptr;
        for (const NoCodePlugin& plugin : ed.plugins)
            if (deps.pluginIds.contains(plugin.id) && !scannedPlugins.contains(plugin.id)) { next = &plugin; break; }
        if (!next) break;
        scannedPlugins.insert(next->id);
        for (const QString& dependency : next->dependencies) deps.pluginIds.insert(dependency);
        for (const PluginCommand& pluginCommand : next->commands) {
            for (const PluginField& field : pluginCommand.fields) {
                const QVariant value = field.defaultValue;
                if (field.type == QLatin1String("switch")) deps.switches.insert(value.toInt());
                else if (field.type == QLatin1String("variable")) deps.variables.insert(value.toInt());
                else if (field.type == QLatin1String("map")) deps.mapIds.insert(value.toString());
                else if (field.type == QLatin1String("actor")) deps.rpgRecords.insert(QStringLiteral("actors|")+value.toString());
                else if (field.type == QLatin1String("item")) deps.rpgRecords.insert(QStringLiteral("items|")+value.toString());
                else if (field.type == QLatin1String("troop")) deps.rpgRecords.insert(QStringLiteral("troops|")+value.toString());
                else if (field.type == QLatin1String("quest")) deps.rpgRecords.insert(QStringLiteral("quests|")+value.toString());
                else if (field.type == QLatin1String("state")) deps.rpgRecords.insert(QStringLiteral("states|")+value.toString());
                else if (field.type == QLatin1String("skill")) deps.rpgRecords.insert(QStringLiteral("skills|")+value.toString());
                else if (field.type == QLatin1String("animation")) deps.rpgRecords.insert(QStringLiteral("animations|")+value.toString());
                collectVariantDependencies(value, deps);
            }
            for (const EventCommand& command : pluginCommand.commands) collectCommandDependencies(ed, command, deps);
        }
    }
    bool dbChanged=true;
    while(dbChanged){
        dbChanged=false; const QSet<QString> current=deps.customDbIds;
        for(const QString& dbId:current) if(const CustomDatabaseDefinition* db=customDatabaseById(ed.customDatabases,dbId))
            for(const CustomDatabaseField& field:db->fields) if(!field.referenceDatabaseId.isEmpty()&&!deps.customDbIds.contains(field.referenceDatabaseId)){deps.customDbIds.insert(field.referenceDatabaseId);dbChanged=true;}
    }
    QJsonObject package{{QStringLiteral("format"),QStringLiteral("ludo-common-event")},{QStringLiteral("version"),1},
        {QStringLiteral("engine"),QString::fromLatin1(version::Engine)},{QStringLiteral("rootCommonEventId"),rootEvent->id},
        {QStringLiteral("rootCommonEventName"),rootEvent->name}};
    QJsonArray common; for(const CommonEvent& c:ed.commonEvents)if(deps.commonIds.contains(c.id))common.append(commonEventToJson(c)); package[QStringLiteral("commonEvents")]=common;
    QJsonObject dependencies;
    QJsonArray switches; for(const SwitchDef& s:ed.switches)if(deps.switches.contains(s.id))switches.append(QJsonObject{{"id",s.id},{"name",s.name},{"initial",s.initial}}); dependencies[QStringLiteral("switches")]=switches;
    QJsonArray variables; for(const VariableDef& v:ed.variables)if(deps.variables.contains(v.id))variables.append(QJsonObject{{"id",v.id},{"name",v.name},{"initial",v.initial}}); dependencies[QStringLiteral("variables")]=variables;
    QJsonArray strings; for(const StringDef& s:ed.strings)if(deps.strings.contains(s.id))strings.append(QJsonObject{{"id",s.id},{"name",s.name},{"initial",s.initial}}); dependencies[QStringLiteral("strings")]=strings;
    QJsonArray custom; for(const CustomDatabaseDefinition& db:ed.customDatabases)if(deps.customDbIds.contains(db.id))custom.append(customDatabaseToJson(db)); dependencies[QStringLiteral("customDatabases")]=custom;
    QJsonArray plugins; for(const NoCodePlugin& p:ed.plugins)if(deps.pluginIds.contains(p.id))plugins.append(noCodePluginToJson(p)); dependencies[QStringLiteral("plugins")]=plugins;
    QJsonArray maps; for(const QString& id:deps.mapIds){if(const MapDoc* map=ed.mapById(id))maps.append(QJsonObject{{"id",map->id},{"name",map->name}});else maps.append(QJsonObject{{"id",id},{"name",QString()}});} dependencies[QStringLiteral("maps")]=maps;
    QJsonArray rpgRecords; for(const QString& token:deps.rpgRecords){const int sep=token.indexOf(QLatin1Char('|'));const QString category=token.left(sep),id=token.mid(sep+1);QString name;int number=0;for(const DatabaseRecord& record:ed.database.value(category))if(record.id==id){name=record.name;number=record.number;break;}rpgRecords.append(QJsonObject{{"category",category},{"id",id},{"name",name},{"number",number}});} dependencies[QStringLiteral("databaseRecords")]=rpgRecords;
    QJsonArray tilesets; for(const QString& id:deps.tilesetIds){QString name;for(const Tileset& tileset:ed.tilesets)if(tileset.id==id){name=tileset.name;break;}tilesets.append(QJsonObject{{"id",id},{"name",name}});} dependencies[QStringLiteral("tilesets")]=tilesets;
    QJsonArray footstepSurfaces; for(const QString& id:deps.footstepSurfaceIds)if(const FootstepSurface* surface=ed.footstepSurfaceById(id))footstepSurfaces.append(footstepSurfaceToJson(ed,*surface)); dependencies[QStringLiteral("footstepSurfaces")]=footstepSurfaces;
    package[QStringLiteral("dependencies")]=dependencies;

    QSet<QString> assetPaths; collectJsonAssetPaths(package,assetPaths); QJsonArray assets; qint64 total=0; QStringList warnings;
    for(const QString& id:deps.commonIds)if(!ed.commonEventById(id))warnings<<QCoreApplication::translate("CommonEventPackage","Dependência de Evento Comum ausente: %1.").arg(id);
    for(const QString& id:deps.customDbIds)if(!customDatabaseById(ed.customDatabases,id))warnings<<QCoreApplication::translate("CommonEventPackage","Dependência de banco personalizado ausente: %1.").arg(id);
    for(const QString& id:deps.pluginIds)if(!noCodePluginById(ed.plugins,id))warnings<<QCoreApplication::translate("CommonEventPackage","Dependência de extensão No-Code ausente: %1.").arg(id);
    for(const QString& id:deps.footstepSurfaceIds)if(!ed.footstepSurfaceById(id))warnings<<QCoreApplication::translate("CommonEventPackage","Dependência de superfície de passos ausente: %1.").arg(id);
    const QString projectRoot=ed.projectRoot();
    for(const QString& relative:assetPaths){
        const QString absolute=QDir(projectRoot).filePath(relative); QFile f(absolute);
        if(!f.exists()){warnings<<QCoreApplication::translate("CommonEventPackage","Asset ausente: %1").arg(relative);continue;}
        if(f.size()>kMaxEmbeddedAssetBytes){warnings<<QCoreApplication::translate("CommonEventPackage","Asset grande demais para embutir: %1").arg(relative);continue;}
        if(!f.open(QIODevice::ReadOnly)){warnings<<QCoreApplication::translate("CommonEventPackage","Não foi possível ler o asset: %1").arg(relative);continue;}
        const QByteArray bytes=f.readAll(); if(total+bytes.size()>kMaxTotalAssetBytes){warnings<<QCoreApplication::translate("CommonEventPackage","Limite total de assets embutidos atingido; %1 ficou externo.").arg(relative);continue;}
        total+=bytes.size(); const QByteArray hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        assets.append(QJsonObject{{"path",relative},{"sha256",QString::fromLatin1(hash)},{"size",double(bytes.size())},{"data",QString::fromLatin1(bytes.toBase64())}});
    }
    package[QStringLiteral("assets")]=assets;
    if(!warnings.isEmpty()){QJsonArray arr;for(const QString& w:warnings)arr.append(w);package[QStringLiteral("warnings")]=arr;}
    const QByteArray data=QJsonDocument(package).toJson(QJsonDocument::Indented);
    if(data.size()>kMaxPackageBytes){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote excederia o limite de segurança de 192 MB.");return false;}
    QSaveFile file(path); if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    if(file.write(data)!=data.size()||!file.commit()){if(error)*error=file.errorString();return false;}
    if(summary){summary->commonEvents=common.size();summary->switches=switches.size();summary->variables=variables.size();summary->strings=strings.size();summary->customDatabases=custom.size();summary->plugins=plugins.size();summary->maps=maps.size();summary->databaseRecords=rpgRecords.size();summary->tilesets=tilesets.size();summary->footstepSurfaces=footstepSurfaces.size();summary->assets=assets.size();summary->assetBytes=total;summary->warnings=warnings;}
    return true;
}

bool importCommonEventPackage(Editor& ed, const QString& path, QString* error,
                              CommonEventPackageImportResult* result)
{
    QFile file(path); if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return false;} if(file.size()>kMaxPackageBytes){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote excede o limite de segurança de 192 MB.");return false;}
    QJsonParseError parse; const QJsonDocument doc=QJsonDocument::fromJson(file.readAll(),&parse); if(parse.error!=QJsonParseError::NoError||!doc.isObject()){if(error)*error=parse.errorString();return false;}
    const QJsonObject package=doc.object(); if(package.value(QStringLiteral("format")).toString()!=QLatin1String("ludo-common-event")){if(error)*error=QCoreApplication::translate("CommonEventPackage","Este arquivo não é um .ludocommon da LUDO.");return false;}
    if(package.value(QStringLiteral("version")).toInt()!=1){if(error)*error=QCoreApplication::translate("CommonEventPackage","Versão de .ludocommon não suportada.");return false;}
    const QJsonObject deps=package.value(QStringLiteral("dependencies")).toObject(); QStringList warnings;
    if (package.value(QStringLiteral("commonEvents")).toArray().size() > kMaxPortableCommonEvents) {
        if (error) *error = QCoreApplication::translate("CommonEventPackage", "O pacote contém Eventos Comuns demais.");
        return false;
    }
    const QStringList boundedArrays = {QStringLiteral("switches"), QStringLiteral("variables"), QStringLiteral("strings"),
        QStringLiteral("customDatabases"), QStringLiteral("plugins"), QStringLiteral("maps"),
        QStringLiteral("databaseRecords"), QStringLiteral("tilesets"), QStringLiteral("footstepSurfaces")};
    for (const QString& key : boundedArrays) if (deps.value(key).toArray().size() > kMaxPortableDependencyItems) {
        if (error) *error = QCoreApplication::translate("CommonEventPackage", "O pacote excede o limite de dependências em %1.").arg(key);
        return false;
    }
    const QJsonArray portableEvents=package.value(QStringLiteral("commonEvents")).toArray();
    QSet<QString> portableCommonIds; QSet<int> portableCommonNumbers;
    for(const QJsonValue& value:portableEvents){
        if(!value.isObject()){if(error)*error=QCoreApplication::translate("CommonEventPackage","Há um Evento Comum inválido no pacote.");return false;}
        const QJsonObject object=value.toObject();const QString id=object.value(QStringLiteral("id")).toString();const int number=object.value(QStringLiteral("number")).toInt();
        if(id.isEmpty()||portableCommonIds.contains(id)||number<=0||portableCommonNumbers.contains(number)){
            if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote possui IDs ou números de Eventos Comuns inválidos/duplicados.");return false;
        }
        if(object.value(QStringLiteral("commands")).toArray().size()>kMaxCommandsPerCommonEvent){if(error)*error=QCoreApplication::translate("CommonEventPackage","Um Evento Comum excede o limite de comandos do pacote.");return false;}
        portableCommonIds.insert(id);portableCommonNumbers.insert(number);
    }
    const QString oldRootId=package.value(QStringLiteral("rootCommonEventId")).toString();
    if(oldRootId.isEmpty()||!portableCommonIds.contains(oldRootId)){if(error)*error=QCoreApplication::translate("CommonEventPackage","O Evento Comum raiz do pacote está ausente ou inválido.");return false;}
    for(const QString& key:{QStringLiteral("switches"),QStringLiteral("variables"),QStringLiteral("strings")}){
        QSet<int> ids;for(const QJsonValue& value:deps.value(key).toArray()){const int id=value.toObject().value(QStringLiteral("id")).toInt();if(id<=0||ids.contains(id)){if(error)*error=QCoreApplication::translate("CommonEventPackage","A lista %1 contém IDs inválidos ou duplicados.").arg(key);return false;}ids.insert(id);}
    }
    QSet<QString> portableDbIds;
    for(const QJsonValue& value:deps.value(QStringLiteral("customDatabases")).toArray()){
        const QJsonObject object=value.toObject();
        const QString dbId=object.value(QStringLiteral("id")).toString();if(dbId.isEmpty()||portableDbIds.contains(dbId)){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote possui IDs de banco personalizado inválidos/duplicados.");return false;}portableDbIds.insert(dbId);
        if(object.value(QStringLiteral("fields")).toArray().size()>kMaxFieldsPerCustomDatabase||object.value(QStringLiteral("records")).toArray().size()>kMaxRecordsPerCustomDatabase){
            if(error)*error=QCoreApplication::translate("CommonEventPackage","Um banco personalizado excede os limites de campos/registros do pacote.");return false;
        }
        QSet<QString> fieldIds,recordIds;for(const QJsonValue& fieldValue:object.value(QStringLiteral("fields")).toArray()){const QString id=fieldValue.toObject().value(QStringLiteral("id")).toString();if(id.isEmpty()||fieldIds.contains(id)){if(error)*error=QCoreApplication::translate("CommonEventPackage","Um banco personalizado possui IDs de campo inválidos/duplicados.");return false;}fieldIds.insert(id);}for(const QJsonValue& recordValue:object.value(QStringLiteral("records")).toArray()){const QString id=recordValue.toObject().value(QStringLiteral("id")).toString();if(id.isEmpty()||recordIds.contains(id)){if(error)*error=QCoreApplication::translate("CommonEventPackage","Um banco personalizado possui IDs de registro inválidos/duplicados.");return false;}recordIds.insert(id);}
    }
    QSet<QString> portableFootstepIds;
    for (const QJsonValue& value : deps.value(QStringLiteral("footstepSurfaces")).toArray()) {
        if (!value.isObject()) { if(error)*error=QCoreApplication::translate("CommonEventPackage","Há uma superfície de passos inválida no pacote."); return false; }
        const QJsonObject object=value.toObject(); const QString id=object.value(QStringLiteral("id")).toString();
        if(id.isEmpty()||portableFootstepIds.contains(id)){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote possui IDs de superfície de passos inválidos/duplicados.");return false;}
        if(object.value(QStringLiteral("sounds")).toArray().size()>kMaxPortableAssets){if(error)*error=QCoreApplication::translate("CommonEventPackage","Uma superfície de passos possui variações demais.");return false;}
        portableFootstepIds.insert(id);
    }
    const QJsonArray assetArray=package.value(QStringLiteral("assets")).toArray();
    if(assetArray.size()>kMaxPortableAssets){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote contém assets demais.");return false;}
    QVector<EmbeddedAsset> preparedAssets;preparedAssets.reserve(assetArray.size());qint64 preparedAssetBytes=0;
    for(const QJsonValue& value:assetArray){
        if(!value.isObject()){if(error)*error=QCoreApplication::translate("CommonEventPackage","Há um asset inválido no pacote.");return false;}
        const QJsonObject object=value.toObject();EmbeddedAsset asset;asset.path=QDir::cleanPath(QDir::fromNativeSeparators(object.value(QStringLiteral("path")).toString()));
        if(!isSafeAssetPath(asset.path)||asset.path==QLatin1String("Assets")){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote contém caminho de asset inseguro: %1").arg(asset.path);return false;}
        asset.bytes=QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1());
        if(asset.bytes.size()>kMaxEmbeddedAssetBytes||preparedAssetBytes+asset.bytes.size()>kMaxTotalAssetBytes){if(error)*error=QCoreApplication::translate("CommonEventPackage","Os assets do pacote excedem os limites de segurança.");return false;}
        const qint64 declared=qint64(object.value(QStringLiteral("size")).toDouble(-1));
        if(declared>=0&&declared!=asset.bytes.size()){if(error)*error=QCoreApplication::translate("CommonEventPackage","O tamanho declarado do asset não confere: %1").arg(asset.path);return false;}
        asset.sha256=QCryptographicHash::hash(asset.bytes,QCryptographicHash::Sha256).toHex();const QByteArray declaredHash=object.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
        if(declaredHash.size()!=64||declaredHash!=asset.sha256){if(error)*error=QCoreApplication::translate("CommonEventPackage","A integridade SHA-256 do asset falhou: %1").arg(asset.path);return false;}
        preparedAssetBytes+=asset.bytes.size();preparedAssets.push_back(asset);
    }
    QVector<NoCodePlugin> sourcePlugins;QSet<QString> sourcePluginIds;
    for(const QJsonValue& value:deps.value(QStringLiteral("plugins")).toArray()){
        NoCodePlugin plugin;QString parseError;
        if(!value.isObject()||!noCodePluginFromJson(value.toObject(),&plugin,&parseError)){if(error)*error=parseError.isEmpty()?QCoreApplication::translate("CommonEventPackage","Há uma extensão No-Code inválida no pacote."):parseError;return false;}
        if(sourcePluginIds.contains(plugin.id)){if(error)*error=QCoreApplication::translate("CommonEventPackage","O pacote contém a extensão No-Code duplicada: %1").arg(plugin.id);return false;}
        sourcePluginIds.insert(plugin.id);
        if(const NoCodePlugin* existing=noCodePluginById(ed.plugins,plugin.id)){
            // Reimportar o mesmo pacote deve preservar os bindings locais já remapeados
            // na primeira importação. O plugin instalado é a autoridade: nunca o
            // sobrescrevemos silenciosamente. Uma versão diferente continua sendo
            // conflito; na mesma versão, exigimos que todos os command IDs do pacote
            // existam e reutilizamos a instalação local. Diferenças restantes são
            // esperadas quando Switch/Variable/String etc. foram remapeados no destino.
            if(existing->version!=plugin.version){
                if(error)*error=QCoreApplication::translate("CommonEventPackage","A extensão No-Code “%1” já existe neste projeto em outra versão (%2; pacote: %3). Atualize ou remova a extensão conflitante antes de importar.").arg(plugin.name,existing->version,plugin.version);
                return false;
            }
            for(const PluginCommand& command:plugin.commands){
                if(!pluginCommandById(*existing,command.id)){
                    if(error)*error=QCoreApplication::translate("CommonEventPackage","A extensão No-Code “%1” instalada não possui o comando “%2” exigido pelo pacote.").arg(plugin.name,command.id);
                    return false;
                }
            }
            QJsonObject installed=noCodePluginToJson(*existing),incoming=noCodePluginToJson(plugin);installed.remove(QStringLiteral("enabled"));incoming.remove(QStringLiteral("enabled"));
            if(installed!=incoming)
                warnings<<QCoreApplication::translate("CommonEventPackage","A extensão No-Code “%1” já estava instalada na mesma versão e foi reutilizada preservando os bindings locais do projeto.").arg(plugin.name);
        }
        sourcePlugins.push_back(plugin);
    }
    for(const QJsonValue& w:package.value(QStringLiteral("warnings")).toArray())warnings<<w.toString();
    const QString packageEngine=package.value(QStringLiteral("engine")).toString();
    if(!packageEngine.isEmpty()&&packageEngine!=QString::fromLatin1(version::Engine))
        warnings<<QCoreApplication::translate("CommonEventPackage","Pacote criado na LUDO %1; importado na %2. O formato portátil é compatível, mas valide os comandos no Diagnóstico.")
            .arg(packageEngine,QString::fromLatin1(version::Engine));

    QHash<int,int> switchMap,variableMap,stringMap; QSet<int> usedSwitches,usedVariables,usedStrings;
    for(const SwitchDef& s:ed.switches)usedSwitches.insert(s.id); for(const VariableDef& v:ed.variables)usedVariables.insert(v.id); for(const StringDef& s:ed.strings)usedStrings.insert(s.id);
    for(const QJsonValue& value:deps.value(QStringLiteral("switches")).toArray()){
        const QJsonObject o=value.toObject();const int old=o.value(QStringLiteral("id")).toInt();const QString name=o.value(QStringLiteral("name")).toString().left(128);const bool initial=o.value(QStringLiteral("initial")).toBool(false);int id=old;
        const auto same=std::find_if(ed.switches.cbegin(),ed.switches.cend(),[&](const SwitchDef& item){return item.id==old;});
        if(same!=ed.switches.cend()&&(same->name!=name||same->initial!=initial)){
            int matched=0,matches=0;for(const SwitchDef& item:ed.switches)if(item.name==name&&item.initial==initial){matched=item.id;++matches;}
            if(matches==1){id=matched;warnings<<QCoreApplication::translate("CommonEventPackage","Switch “%1” já existia como #%2 e foi reutilizado.").arg(name).arg(id);}
            else{id=nextFreeInt(usedSwitches);warnings<<QCoreApplication::translate("CommonEventPackage","Switch #%1 colidiu e foi importado como #%2.").arg(old).arg(id);}
        }
        if(!usedSwitches.contains(id)){ed.switches.push_back(SwitchDef{id,name,initial});usedSwitches.insert(id);}switchMap[old]=id;
    }
    for(const QJsonValue& value:deps.value(QStringLiteral("variables")).toArray()){
        const QJsonObject o=value.toObject();const int old=o.value(QStringLiteral("id")).toInt();const QString name=o.value(QStringLiteral("name")).toString().left(128);const int initial=o.value(QStringLiteral("initial")).toInt();int id=old;
        const auto same=std::find_if(ed.variables.cbegin(),ed.variables.cend(),[&](const VariableDef& item){return item.id==old;});
        if(same!=ed.variables.cend()&&(same->name!=name||same->initial!=initial)){
            int matched=0,matches=0;for(const VariableDef& item:ed.variables)if(item.name==name&&item.initial==initial){matched=item.id;++matches;}
            if(matches==1){id=matched;warnings<<QCoreApplication::translate("CommonEventPackage","Variável “%1” já existia como #%2 e foi reutilizada.").arg(name).arg(id);}
            else{id=nextFreeInt(usedVariables);warnings<<QCoreApplication::translate("CommonEventPackage","Variável #%1 colidiu e foi importada como #%2.").arg(old).arg(id);}
        }
        if(!usedVariables.contains(id)){ed.variables.push_back(VariableDef{id,name,initial});usedVariables.insert(id);}variableMap[old]=id;
    }
    for(const QJsonValue& value:deps.value(QStringLiteral("strings")).toArray()){
        const QJsonObject o=value.toObject();const int old=o.value(QStringLiteral("id")).toInt();const QString name=o.value(QStringLiteral("name")).toString().left(128);const QString initial=o.value(QStringLiteral("initial")).toString().left(65535);int id=old;
        const auto same=std::find_if(ed.strings.cbegin(),ed.strings.cend(),[&](const StringDef& item){return item.id==old;});
        if(same!=ed.strings.cend()&&(same->name!=name||same->initial!=initial)){
            int matched=0,matches=0;for(const StringDef& item:ed.strings)if(item.name==name&&item.initial==initial){matched=item.id;++matches;}
            if(matches==1){id=matched;warnings<<QCoreApplication::translate("CommonEventPackage","String “%1” já existia como #%2 e foi reutilizada.").arg(name).arg(id);}
            else{id=nextFreeInt(usedStrings);warnings<<QCoreApplication::translate("CommonEventPackage","String #%1 colidiu e foi importada como #%2.").arg(old).arg(id);}
        }
        if(!usedStrings.contains(id)){StringDef item;item.id=id;item.name=name;item.initial=initial;ed.strings.push_back(item);usedStrings.insert(id);}stringMap[old]=id;
    }

    QVector<CustomDatabaseDefinition> sourceDbs; for(const QJsonValue& value:deps.value(QStringLiteral("customDatabases")).toArray())sourceDbs.push_back(customDatabaseFromJson(value.toObject()));
    QHash<QString,QString> dbMap,fieldMap,recordMap; QVector<CustomDatabaseDefinition> newDbs;
    for(CustomDatabaseDefinition source:sourceDbs){
        const QString oldDbId=source.id;const CustomDatabaseDefinition* existing=customDatabaseById(ed.customDatabases,oldDbId);
        if(existing&&customDatabaseToJson(*existing)==customDatabaseToJson(source)){
            dbMap[oldDbId]=oldDbId;for(const CustomDatabaseField& field:source.fields)fieldMap[field.id]=field.id;for(const CustomDatabaseRecord& record:source.records)recordMap[record.id]=record.id;continue;
        }
        if(existing){
            source.id=idGen();dbMap[oldDbId]=source.id;
            warnings<<QCoreApplication::translate("CommonEventPackage","Banco “%1” tinha ID conflitante com conteúdo diferente e foi importado como uma cópia independente.").arg(source.name);
            QHash<QString,QString> localFields;
            for(CustomDatabaseField& field:source.fields){const QString old=field.id;field.id=idGen();fieldMap[old]=field.id;localFields[old]=field.id;}
            for(CustomDatabaseRecord& record:source.records){const QString old=record.id;record.id=idGen();recordMap[old]=record.id;QVariantMap values;for(auto it=record.values.cbegin();it!=record.values.cend();++it)values[localFields.value(it.key(),it.key())]=it.value();record.values=values;}
        }else{
            dbMap[oldDbId]=oldDbId;for(const CustomDatabaseField& field:source.fields)fieldMap[field.id]=field.id;for(const CustomDatabaseRecord& record:source.records)recordMap[record.id]=record.id;
        }
        newDbs.push_back(source);
    }
    for(CustomDatabaseDefinition& db:newDbs){
        for(CustomDatabaseField& field:db.fields){field.referenceDatabaseId=dbMap.value(field.referenceDatabaseId,field.referenceDatabaseId);if(field.type==CustomDatabaseFieldType::RecordReference)field.defaultValue=recordMap.value(field.defaultValue.toString(),field.defaultValue.toString());}
        for(CustomDatabaseRecord& record:db.records)for(const CustomDatabaseField& field:db.fields)if(field.type==CustomDatabaseFieldType::RecordReference&&record.values.contains(field.id))record.values[field.id]=recordMap.value(record.values.value(field.id).toString(),record.values.value(field.id).toString());
        normalizeCustomDatabaseDefinition(db);ed.customDatabases.push_back(db);
    }

    QHash<QString,QString> footstepMap;
    QVector<FootstepSurface> newFootsteps;
    for (const QJsonValue& value : deps.value(QStringLiteral("footstepSurfaces")).toArray()) {
        FootstepSurface source = footstepSurfaceFromJson(value.toObject());
        const QString oldId = source.id;
        if (const FootstepSurface* existing = ed.footstepSurfaceById(oldId)) {
            if (footstepSurfaceToJson(ed,*existing) == value.toObject()) {
                footstepMap[oldId] = oldId;
                continue;
            }
            source.id = idGen(); footstepMap[oldId] = source.id;
            warnings << QCoreApplication::translate("CommonEventPackage","A superfície de passos “%1” tinha ID conflitante e foi importada como cópia independente.").arg(source.name);
        } else footstepMap[oldId] = oldId;
        newFootsteps.push_back(source);
    }

    QHash<QString,QString> mapMap; for(const QJsonValue& value:deps.value(QStringLiteral("maps")).toArray()){const QJsonObject o=value.toObject();const QString old=o.value("id").toString();if(ed.mapById(old)){mapMap[old]=old;continue;}QString match;const QString name=o.value("name").toString();for(const MapDoc& map:ed.docs)if(!name.isEmpty()&&map.name.compare(name,Qt::CaseInsensitive)==0){if(match.isEmpty())match=map.id;else{match.clear();break;}}if(!match.isEmpty()){mapMap[old]=match;warnings<<QCoreApplication::translate("CommonEventPackage","Mapa “%1” foi associado por nome.").arg(name);}else{mapMap[old]=old;warnings<<QCoreApplication::translate("CommonEventPackage","Dependência de mapa não resolvida: %1.").arg(name.isEmpty()?old:name);}}

    QHash<QString,QString> rpgMap;
    for (const QJsonValue& value : deps.value(QStringLiteral("databaseRecords")).toArray()) {
        const QJsonObject o = value.toObject(); const QString category = o.value(QStringLiteral("category")).toString();
        const QString old = o.value(QStringLiteral("id")).toString(); const QString token = category + QLatin1Char('|') + old;
        const QVector<DatabaseRecord>& records = ed.database.value(category);
        bool exact = false; for (const DatabaseRecord& record : records) if (record.id == old) { exact = true; break; }
        if (exact) { rpgMap[token] = old; continue; }
        QString match; const QString name = o.value(QStringLiteral("name")).toString();
        for (const DatabaseRecord& record : records) if (!name.isEmpty() && record.name.compare(name, Qt::CaseInsensitive) == 0) {
            if (match.isEmpty()) match = record.id; else { match.clear(); break; }
        }
        if (!match.isEmpty()) {
            rpgMap[token] = match; warnings << QCoreApplication::translate("CommonEventPackage", "Registro “%1” (%2) foi associado por nome.").arg(name, databaseCategoryLabel(category));
        } else {
            rpgMap[token] = old; warnings << QCoreApplication::translate("CommonEventPackage", "Dependência do banco RPG não resolvida: %1 / %2.").arg(databaseCategoryLabel(category), name.isEmpty() ? old : name);
        }
    }
    QHash<QString,QString> tilesetMap;
    for (const QJsonValue& value : deps.value(QStringLiteral("tilesets")).toArray()) {
        const QJsonObject o = value.toObject(); const QString old = o.value(QStringLiteral("id")).toString();
        bool exact = false; for (const Tileset& tileset : ed.tilesets) if (tileset.id == old) { exact = true; break; }
        if (exact) { tilesetMap[old] = old; continue; }
        QString match; const QString name = o.value(QStringLiteral("name")).toString();
        for (const Tileset& tileset : ed.tilesets) if (!name.isEmpty() && tileset.name.compare(name, Qt::CaseInsensitive) == 0) {
            if (match.isEmpty()) match = tileset.id; else { match.clear(); break; }
        }
        if (!match.isEmpty()) { tilesetMap[old] = match; warnings << QCoreApplication::translate("CommonEventPackage", "Tileset “%1” foi associado por nome.").arg(name); }
        else { tilesetMap[old] = old; warnings << QCoreApplication::translate("CommonEventPackage", "Dependência de tileset não resolvida: %1.").arg(name.isEmpty() ? old : name); }
    }

    QHash<QString,QString> assetMap; qint64 assetBytes=preparedAssetBytes; int importedAssets=0; const QString rootToken=oldRootId;
    bool warnedUnsavedAssets=false;
    for(const EmbeddedAsset& asset:preparedAssets){
        const QString old=asset.path;const QByteArray& bytes=asset.bytes;
        if(ed.projectPath.isEmpty()){
            assetMap[old]=old;
            if(!warnedUnsavedAssets){warnings<<QCoreApplication::translate("CommonEventPackage","Projeto ainda não foi salvo; assets embutidos não puderam ser gravados.");warnedUnsavedAssets=true;}
            continue;
        }
        QString destination=old;const QString absolute=QDir(ed.projectRoot()).filePath(destination);
        if(QFileInfo::exists(absolute)){
            QFile existing(absolute);
            if(!existing.open(QIODevice::ReadOnly)||QCryptographicHash::hash(existing.readAll(),QCryptographicHash::Sha256).toHex()!=asset.sha256)
                destination=safeImportedAssetPath(rootToken,old,asset.sha256);
        }
        const QString finalPath=QDir(ed.projectRoot()).filePath(destination);QDir().mkpath(QFileInfo(finalPath).absolutePath());
        if(!QFileInfo::exists(finalPath)){
            QSaveFile out(finalPath);if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit()){warnings<<QCoreApplication::translate("CommonEventPackage","Falha ao gravar asset: %1").arg(destination);continue;}++importedAssets;
        }
        assetMap[old]=destination;
    }

    QVector<CommonEvent> sourceEvents; for(const QJsonValue& value:portableEvents)sourceEvents.push_back(commonEventFromJson(value.toObject()));
    QHash<QString,QString> commonMap;QHash<int,int> commonNumberMap;QSet<int> usedNumbers;QSet<QString> usedCommonIds;
    for(const CommonEvent& c:ed.commonEvents){usedNumbers.insert(c.number);usedCommonIds.insert(c.id);}
    for(CommonEvent& c:sourceEvents){
        const QString oldId=c.id;const int oldNumber=c.number;
        if(usedCommonIds.contains(c.id)){c.id=idGen();warnings<<QCoreApplication::translate("CommonEventPackage","Evento Comum “%1” tinha ID conflitante e recebeu um novo ID.").arg(c.name);}
        usedCommonIds.insert(c.id);commonMap[oldId]=c.id;
        int number=oldNumber;if(usedNumbers.contains(number))number=nextFreeInt(usedNumbers);usedNumbers.insert(number);commonNumberMap[oldNumber]=number;c.number=number;
        if(number!=oldNumber)warnings<<QCoreApplication::translate("CommonEventPackage","Evento Comum #%1 foi importado como #%2.").arg(oldNumber).arg(number);
    }

    QVector<NoCodePlugin> importedPlugins;for(const NoCodePlugin& plugin:sourcePlugins)if(!noCodePluginById(ed.plugins,plugin.id))importedPlugins.push_back(plugin);
    for(const NoCodePlugin& plugin:sourcePlugins)for(const QString& dependency:plugin.dependencies)if(!sourcePluginIds.contains(dependency)&&!noCodePluginById(ed.plugins,dependency))warnings<<QCoreApplication::translate("CommonEventPackage","A extensão “%1” depende de “%2”, que não está instalada nem veio no pacote.").arg(plugin.name,dependency);

    for(CommonEvent& c:sourceEvents){
        if(!c.advancedTrigger&&switchMap.contains(c.switchId))c.switchId=switchMap.value(c.switchId);
        QVariant l=c.triggerLeft,r=c.triggerRight;
        rewriteVariant(l,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap);
        rewriteVariant(r,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap);
        rewriteExternalProjectRefs(l,rpgMap,tilesetMap); rewriteExternalProjectRefs(r,rpgMap,tilesetMap);
        rewriteFootstepRefs(l,footstepMap); rewriteFootstepRefs(r,footstepMap);
        c.triggerLeft=l.toMap();c.triggerRight=r.toMap();
        for(EventCommand& command:c.commands){rewriteCommand(command,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap);QVariant params=command.params;rewriteExternalProjectRefs(params,rpgMap,tilesetMap);rewriteFootstepRefs(params,footstepMap);command.params=params.toMap();}
        ed.commonEvents.push_back(c);
    }
    for(NoCodePlugin& plugin:importedPlugins){
        for(PluginCommand& pc:plugin.commands){
            for(PluginField& field:pc.fields){
                QVariant value=field.defaultValue;rewriteVariant(value,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap);rewriteFootstepRefs(value,footstepMap);
                if(field.type==QLatin1String("switch"))value=switchMap.value(value.toInt(),value.toInt());
                else if(field.type==QLatin1String("variable"))value=variableMap.value(value.toInt(),value.toInt());
                else if(field.type==QLatin1String("map"))value=mapMap.value(value.toString(),value.toString());
                else{
                    QString category;if(field.type==QLatin1String("actor"))category=QStringLiteral("actors");else if(field.type==QLatin1String("item"))category=QStringLiteral("items");else if(field.type==QLatin1String("troop"))category=QStringLiteral("troops");else if(field.type==QLatin1String("quest"))category=QStringLiteral("quests");else if(field.type==QLatin1String("state"))category=QStringLiteral("states");else if(field.type==QLatin1String("skill"))category=QStringLiteral("skills");else if(field.type==QLatin1String("animation"))category=QStringLiteral("animations");
                    if(!category.isEmpty()){const QString old=value.toString();value=rpgMap.value(category+QLatin1Char('|')+old,old);}
                }
                field.defaultValue=value;
            }
            for(EventCommand& command:pc.commands){rewriteCommand(command,switchMap,variableMap,stringMap,commonMap,commonNumberMap,dbMap,fieldMap,recordMap,mapMap,assetMap);QVariant params=command.params;rewriteExternalProjectRefs(params,rpgMap,tilesetMap);rewriteFootstepRefs(params,footstepMap);command.params=params.toMap();}
        }
        ed.plugins.push_back(plugin);
    }
    if(!ed.projectPath.isEmpty()){QString assetError;if(!ed.assetDatabase.synchronize(ed.projectRoot(),&assetError)&&!assetError.isEmpty())warnings<<assetError;}
    for (FootstepSurface& surface : newFootsteps) {
        for (FootstepSound& sound : surface.sounds) {
            const QString oldPath = QDir::fromNativeSeparators(sound.sourcePath);
            sound.sourcePath = assetMap.value(oldPath, oldPath);
            sound.assetId = ed.assetDatabase.idForPath(sound.sourcePath);
        }
        ed.footstepSurfaces.push_back(surface);
    }
    upgradeCommonEventCallStableIds(ed);ed.markDirty();
    const QString oldRoot=package.value(QStringLiteral("rootCommonEventId")).toString();const QString newRoot=commonMap.value(oldRoot);const CommonEvent* importedRoot=ed.commonEventById(newRoot);
    if(result){result->rootCommonEventId=newRoot;result->rootCommonEventName=importedRoot?importedRoot->name:package.value(QStringLiteral("rootCommonEventName")).toString();result->commonEvents=sourceEvents.size();result->switches=deps.value("switches").toArray().size();result->variables=deps.value("variables").toArray().size();result->strings=deps.value("strings").toArray().size();result->customDatabases=sourceDbs.size();result->plugins=sourcePlugins.size();result->maps=deps.value("maps").toArray().size();result->databaseRecords=deps.value("databaseRecords").toArray().size();result->tilesets=deps.value("tilesets").toArray().size();result->footstepSurfaces=deps.value("footstepSurfaces").toArray().size();result->assets=importedAssets;result->assetBytes=assetBytes;result->warnings=warnings;}
    return !newRoot.isEmpty();
}

} // namespace core
