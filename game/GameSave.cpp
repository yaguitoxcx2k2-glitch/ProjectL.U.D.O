#include "GameSave.h"

#include "core/Editor.h"
#include "core/Version.h"
#include "game/RpgSystem.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <utility>

namespace game {
namespace {

QString safeProjectId(const core::Editor& ed)
{
    QString id = ed.projectId;
    if (id.isEmpty()) {
        id = QString::fromLatin1(QCryptographicHash::hash(ed.projectName.toUtf8(),
                                                          QCryptographicHash::Sha256).toHex().left(24));
    }
    for (QChar& c : id)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    return id;
}

} // namespace

QString gameSavePath(const core::Editor& ed, int slot)
{
    slot = qBound(1, slot, 99);
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    QDir dir(base);
    return dir.filePath(QStringLiteral("LudoEngine/Saves/%1/slot%2.lusave")
                            .arg(safeProjectId(ed))
                            .arg(slot, 2, 10, QLatin1Char('0')));
}

bool writeGameSave(const QString& path, const GameSaveData& data, QString* error)
{
    if (data.mapId.isEmpty()) {
        if (error) *error = QStringLiteral("O save não possui um mapa válido.");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("Não foi possível criar a pasta de saves.");
        return false;
    }

    QJsonObject player;
    player[QStringLiteral("halfX")] = data.playerHalfCell.x();
    player[QStringLiteral("halfY")] = data.playerHalfCell.y();
    player[QStringLiteral("direction")] = data.playerDirection;

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("LudoEngineSave");
    root[QStringLiteral("formatVersion")] = core::version::SaveFormat;
    root[QStringLiteral("engineVersion")] = QString::fromLatin1(core::version::Engine);
    root[QStringLiteral("savedAtUtc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    root[QStringLiteral("projectId")] = data.projectId;
    root[QStringLiteral("projectName")] = data.projectName;
    root[QStringLiteral("mapId")] = data.mapId;
    root[QStringLiteral("playTimeSeconds")] = double(data.playTimeSeconds);
    root[QStringLiteral("player")] = player;
    root[QStringLiteral("state")] = data.state.toJson();
    root[QStringLiteral("runtime")] = data.runtime;

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Não foi possível abrir o slot: %1").arg(file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Não foi possível concluir o save: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool readGameSave(const QString& path, const core::Editor& ed, GameSaveData* data, QString* error)
{
    if (!data) {
        if (error) *error = QStringLiteral("Destino de save inválido.");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Não foi possível abrir o slot: %1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Save corrompido: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("LudoEngineSave")) {
        if (error) *error = QStringLiteral("Este arquivo não é um save do Ludo Engine.");
        return false;
    }
    const int version = root.value(QStringLiteral("formatVersion")).toInt();
    if (version < 1 || version > core::version::SaveFormat) {
        if (error) *error = QStringLiteral("Versão de save não suportada: %1.").arg(version);
        return false;
    }

    GameSaveData loaded;
    loaded.projectId = root.value(QStringLiteral("projectId")).toString();
    loaded.projectName = root.value(QStringLiteral("projectName")).toString();
    if (!loaded.projectId.isEmpty() && !ed.projectId.isEmpty() && loaded.projectId != ed.projectId) {
        if (error) *error = QStringLiteral("Este save pertence a outro projeto.");
        return false;
    }
    loaded.mapId = root.value(QStringLiteral("mapId")).toString();
    const double loadedSeconds = root.value(QStringLiteral("playTimeSeconds")).toDouble();
    loaded.playTimeSeconds = loadedSeconds > 0.0 ? qint64(loadedSeconds) : 0;
    if (ed.mapIndexById(loaded.mapId) < 0) {
        if (error) *error = QStringLiteral("O mapa gravado não existe mais no projeto.");
        return false;
    }
    const QJsonObject player = root.value(QStringLiteral("player")).toObject();
    loaded.playerHalfCell = QPoint(player.value(QStringLiteral("halfX")).toInt(),
                                   player.value(QStringLiteral("halfY")).toInt());
    loaded.playerDirection = player.value(QStringLiteral("direction")).toInt();
    if (!loaded.state.fromJson(root.value(QStringLiteral("state")).toObject(), error)) return false;
    // RC2.43 mantém SaveFormat 3. Saves anteriores não possuem `strings` e
    // projetos podem ganhar novos símbolos globais após um save; completar
    // apenas os IDs ausentes preserva os valores salvos e aplica os defaults
    // do projeto às definições novas.
    loaded.state.reconcileLoadedSave(ed); // RC2.59; antigo ensureGlobalsFrom(ed) permanece alias e inclui Runtime Custom Databases
    if (version >= 3 && root.value(QStringLiteral("runtime")).isObject())
        loaded.runtime = root.value(QStringLiteral("runtime")).toObject();
    for (int index = loaded.state.party().size() - 1; index >= 0; --index)
        if (!databaseRecord(ed, QStringLiteral("actors"), loaded.state.party()[index].actorId))
            loaded.state.party().remove(index);
    loaded.state.ensurePartyFrom(ed); // migração transparente do formato v1
    for (PartyMemberState& member : loaded.state.party()) normalizeMember(ed, member);
    *data = std::move(loaded);
    return true;
}

bool readGameSaveSummary(const QString& path,const core::Editor& ed,GameSaveSummary* summary)
{
    if(!summary)return false;*summary=GameSaveSummary();QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
    QJsonParseError parse{};const QJsonDocument doc=QJsonDocument::fromJson(file.readAll(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject())return false;
    const QJsonObject root=doc.object();if(root.value(QStringLiteral("format")).toString()!=QLatin1String("LudoEngineSave"))return false;
    const int version=root.value(QStringLiteral("formatVersion")).toInt();if(version<1||version>core::version::SaveFormat)return false;
    const QString projectId=root.value(QStringLiteral("projectId")).toString();if(!projectId.isEmpty()&&!ed.projectId.isEmpty()&&projectId!=ed.projectId)return false;
    summary->projectName=root.value(QStringLiteral("projectName")).toString();summary->mapId=root.value(QStringLiteral("mapId")).toString();
    if(const core::MapDoc* map=ed.mapById(summary->mapId))summary->mapName=map->name;else summary->mapName=summary->mapId;
    summary->savedAt=QDateTime::fromString(root.value(QStringLiteral("savedAtUtc")).toString(),Qt::ISODateWithMs).toLocalTime();const double seconds=root.value(QStringLiteral("playTimeSeconds")).toDouble();summary->playTimeSeconds=seconds>0.0?qint64(seconds):0;summary->valid=true;return true;
}

} // namespace game
