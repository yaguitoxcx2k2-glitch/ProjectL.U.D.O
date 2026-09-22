#include "MoveRoutePresetStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace core {
namespace {

MoveRoute pathPreset(const QString& behavior, int minDistance, int maxDistance, bool repeat)
{
    MoveRoute route;
    route.repeat = repeat;
    route.target = QStringLiteral("self");
    route.startMode = MoveRouteStartMode::Replace;
    route.waitForCompletion = false;
    route.blockedPolicy = MoveRouteBlockedPolicy::Wait;
    MoveCommand command;
    command.type = QStringLiteral("pathfind");
    MoveRoutePathOptions options;
    options.behavior = moveRoutePathBehaviorFromId(behavior);
    options.targetKind = MoveRoutePathTargetKind::Player;
    options.minDistance = minDistance;
    options.maxDistance = maxDistance;
    options.diagonal = true;
    options.maxSearchNodes = 4096;
    command.params = moveRoutePathOptionsToParams(options);
    route.commands.push_back(command);
    return route;
}

MoveRoute squarePatrol()
{
    MoveRoute route;
    route.repeat = true;
    route.blockedPolicy = MoveRouteBlockedPolicy::Wait;
    for (const QString& type : {QStringLiteral("moveDown"), QStringLiteral("moveLeft"),
                                QStringLiteral("moveUp"), QStringLiteral("moveRight")})
        route.commands.push_back(MoveCommand{type,{}});
    return route;
}

MoveRoute guardArea()
{
    MoveRoute route;
    route.repeat = true;
    route.blockedPolicy = MoveRouteBlockedPolicy::Wait;
    route.commands = {
        MoveCommand{QStringLiteral("turnRandom"),{}},
        MoveCommand{QStringLiteral("wait"),{{QStringLiteral("frames"),45}}}
    };
    return route;
}

QString cleanName(const QString& source)
{
    QString name = source.simplified();
    if (name.size() > 80) name.truncate(80);
    return name;
}

QJsonObject presetToJson(const MoveRoutePreset& preset)
{
    return QJsonObject{
        {QStringLiteral("id"), preset.id},
        {QStringLiteral("name"), preset.name},
        {QStringLiteral("route"), QJsonObject::fromVariantMap(moveRouteToMap(preset.route))}
    };
}

} // namespace

QString moveRoutePresetFilePath(const QString& projectRoot)
{
    if (projectRoot.trimmed().isEmpty()) return QString();
    return QDir(projectRoot).filePath(QString::fromLatin1(MoveRoutePresetRelativePath));
}

QVector<MoveRoutePreset> builtInMoveRoutePresets()
{
    return {
        {QStringLiteral("builtin.squarePatrol"), QObject::tr("Patrulhar em quadrado"), squarePatrol(), true},
        {QStringLiteral("builtin.followPlayer"), QObject::tr("Seguir o jogador"), pathPreset(QStringLiteral("follow"),0,1,true), true},
        {QStringLiteral("builtin.fleePlayer"), QObject::tr("Fugir do jogador"), pathPreset(QStringLiteral("flee"),4,4,false), true},
        {QStringLiteral("builtin.keepDistance"), QObject::tr("Manter distância do jogador"), pathPreset(QStringLiteral("keepDistance"),2,4,true), true},
        {QStringLiteral("builtin.guardArea"), QObject::tr("Vigiar área"), guardArea(), true}
    };
}

bool normalizeMoveRoutePreset(MoveRoutePreset* preset, QString* error)
{
    if (error) error->clear();
    if (!preset) { if (error) *error = QObject::tr("Preset ausente."); return false; }
    preset->name = cleanName(preset->name);
    if (preset->name.isEmpty()) { if (error) *error = QObject::tr("O preset precisa de um nome."); return false; }
    if (preset->route.commands.isEmpty()) { if (error) *error = QObject::tr("Não é possível salvar uma rota vazia como preset."); return false; }
    if (preset->route.commands.size() > MoveRoutePresetMaxCommands) {
        if (error) *error = QObject::tr("O preset excede o limite seguro de %1 comandos.").arg(MoveRoutePresetMaxCommands);
        return false;
    }
    for (const MoveCommand& command : preset->route.commands) {
        if (!isKnownMoveRouteCommandType(command.type)) {
            if (error) *error = QObject::tr("O preset contém comando desconhecido: %1.").arg(command.type);
            return false;
        }
    }
    preset->id = preset->id.trimmed();
    if (!preset->builtIn && preset->id.startsWith(QStringLiteral("builtin."))) {
        if (error) *error = QObject::tr("O prefixo de ID 'builtin.' é reservado pela LUDO.");
        return false;
    }
    if (preset->id.isEmpty())
        preset->id = QStringLiteral("routepreset.%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    preset->route.startMode = MoveRouteStartMode::Replace;
    preset->route.waitForCompletion = false;
    preset->route.target = QStringLiteral("self");
    return true;
}

QVector<MoveRoutePreset> loadUserMoveRoutePresets(const QString& projectRoot, QString* error)
{
    if (error) error->clear();
    QVector<MoveRoutePreset> out;
    const QString path = moveRoutePresetFilePath(projectRoot);
    if (path.isEmpty()) return out;
    QFile file(path);
    if (!file.exists()) return out;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Não foi possível abrir os presets de rota: %1").arg(file.errorString());
        return out;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QObject::tr("Arquivo de presets de rota inválido: %1").arg(parse.errorString());
        return out;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("LudoMoveRoutePresets")) {
        if (error) *error = QObject::tr("Assinatura do arquivo de presets de rota inválida.");
        return out;
    }
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (version != MoveRoutePresetFileVersion) {
        if (error) *error = version > MoveRoutePresetFileVersion
            ? QObject::tr("Os presets foram criados por uma versão mais nova da LUDO (formato %1).").arg(version)
            : QObject::tr("Formato de presets de rota inválido (%1).").arg(version);
        return out;
    }
    const QJsonArray array = root.value(QStringLiteral("presets")).toArray();
    if (array.size() > MoveRoutePresetMaxCount) {
        if (error) *error = QObject::tr("O arquivo excede o limite seguro de %1 presets.").arg(MoveRoutePresetMaxCount);
        return out;
    }
    QSet<QString> ids;
    int index = 0;
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            if (error) *error = QObject::tr("Preset de rota #%1 não é um objeto válido.").arg(index + 1);
            return {};
        }
        const QJsonObject object = value.toObject();
        MoveRoutePreset preset;
        preset.id = object.value(QStringLiteral("id")).toString().trimmed();
        preset.name = object.value(QStringLiteral("name")).toString();
        if (!object.value(QStringLiteral("route")).isObject()) {
            if (error) *error = QObject::tr("Preset de rota #%1 não possui uma rota válida.").arg(index + 1);
            return {};
        }
        preset.route = moveRouteFromMap(object.value(QStringLiteral("route")).toObject().toVariantMap());
        preset.builtIn = false;
        QString why;
        if (!normalizeMoveRoutePreset(&preset, &why)) {
            if (error) *error = QObject::tr("Preset de rota #%1 inválido: %2").arg(index + 1).arg(why);
            return {};
        }
        if (ids.contains(preset.id)) {
            if (error) *error = QObject::tr("Há IDs de preset duplicados no arquivo: %1.").arg(preset.id);
            return {};
        }
        ids.insert(preset.id);
        out.push_back(preset);
        ++index;
    }
    return out;
}

bool saveUserMoveRoutePresets(const QString& projectRoot,
                              const QVector<MoveRoutePreset>& presets,
                              QString* error)
{
    if (error) error->clear();
    const QString path = moveRoutePresetFilePath(projectRoot);
    if (path.isEmpty()) { if (error) *error = QObject::tr("O projeto precisa estar salvo antes de criar presets de rota."); return false; }
    QVector<MoveRoutePreset> normalized;
    QSet<QString> ids;
    for (MoveRoutePreset preset : presets) {
        if (preset.builtIn) continue;
        QString why;
        if (!normalizeMoveRoutePreset(&preset, &why)) { if (error) *error = why; return false; }
        if (ids.contains(preset.id)) { if (error) *error = QObject::tr("Há IDs de preset duplicados."); return false; }
        ids.insert(preset.id);
        normalized.push_back(preset);
        if (normalized.size() > MoveRoutePresetMaxCount) {
            if (error) *error = QObject::tr("Limite de %1 presets excedido.").arg(MoveRoutePresetMaxCount);
            return false;
        }
    }
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QObject::tr("Não foi possível criar a pasta de presets de rota.");
        return false;
    }
    QJsonArray array;
    for (const MoveRoutePreset& preset : normalized) array.append(presetToJson(preset));
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("LudoMoveRoutePresets")},
        {QStringLiteral("version"), MoveRoutePresetFileVersion},
        {QStringLiteral("presets"), array}
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace core
