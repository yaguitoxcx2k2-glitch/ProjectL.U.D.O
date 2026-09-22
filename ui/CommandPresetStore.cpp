#include "CommandPresetStore.h"

#include "core/Model.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSaveFile>
#include <QSettings>
#include <QSet>

namespace ui {
namespace {
constexpr auto kPresetKey = "commandPicker/presetsJson";

QJsonObject toJson(const CommandPreset& preset)
{
    return {{QStringLiteral("id"), preset.id},
            {QStringLiteral("name"), preset.name},
            {QStringLiteral("commandType"), preset.commandType},
            {QStringLiteral("params"), QJsonObject::fromVariantMap(preset.params)},
            {QStringLiteral("description"), preset.description}};
}

CommandPreset fromJson(const QJsonObject& object)
{
    CommandPreset preset;
    preset.id = object.value(QStringLiteral("id")).toString().trimmed();
    preset.name = object.value(QStringLiteral("name")).toString().trimmed().left(128);
    preset.commandType = object.value(QStringLiteral("commandType")).toString().trimmed().left(128);
    preset.params = object.value(QStringLiteral("params")).toObject().toVariantMap();
    preset.description = object.value(QStringLiteral("description")).toString().left(2048);
    return preset;
}

bool valid(const CommandPreset& preset)
{
    return !preset.id.isEmpty() && !preset.name.isEmpty() && !preset.commandType.isEmpty();
}

void persist(const QVector<CommandPreset>& presets)
{
    QJsonArray array;
    for (const CommandPreset& preset : presets) array.append(toJson(preset));

    // Persiste o JSON como bytes UTF-8. Isso evita round-trip dependente da
    // conversão QString/QSettings no backend INI do Windows (acentos inclusos)
    // e mantém leitura compatível com valores antigos gravados como QString.
    QSettings settings;
    settings.setValue(QString::fromLatin1(kPresetKey),
                      QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings.sync();
}
} // namespace

QVector<CommandPreset> loadCommandPresets()
{
    QSettings settings;
    const QVariant stored = settings.value(QString::fromLatin1(kPresetKey));
    const QByteArray bytes = stored.metaType().id() == QMetaType::QByteArray
        ? stored.toByteArray()
        : stored.toString().toUtf8(); // compatibilidade com RCs anteriores
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    QVector<CommandPreset> presets;
    QSet<QString> ids;
    for (const QJsonValue& value : document.array()) {
        if (presets.size() >= 256) break;
        const CommandPreset preset = fromJson(value.toObject());
        if (!valid(preset) || ids.contains(preset.id)) continue;
        ids.insert(preset.id);
        presets.push_back(preset);
    }
    return presets;
}

bool saveCommandPreset(const CommandPreset& source)
{
    CommandPreset preset = source;
    if (preset.id.isEmpty()) preset.id = core::idGen();
    preset.name = preset.name.trimmed().left(128);
    preset.commandType = preset.commandType.trimmed().left(128);
    if (!valid(preset)) return false;
    QVector<CommandPreset> presets = loadCommandPresets();
    bool replaced = false;
    for (CommandPreset& current : presets) {
        if (current.id != preset.id) continue;
        current = preset; replaced = true; break;
    }
    if (!replaced) {
        if (presets.size() >= 256) return false;
        presets.push_back(preset);
    }
    persist(presets);
    return true;
}

bool removeCommandPreset(const QString& id)
{
    QVector<CommandPreset> presets = loadCommandPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).id != id) continue;
        presets.removeAt(i); persist(presets); return true;
    }
    return false;
}

CommandPreset commandPresetById(const QString& id)
{
    for (const CommandPreset& preset : loadCommandPresets())
        if (preset.id == id) return preset;
    return {};
}

bool exportCommandPresets(const QString& filePath, QString* error)
{
    QJsonArray array;
    for (const CommandPreset& preset : loadCommandPresets()) array.append(toJson(preset));
    QJsonObject root{{QStringLiteral("format"), QStringLiteral("ludo-command-presets")},
                     {QStringLiteral("version"), 1},
                     {QStringLiteral("presets"), array}};
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}

bool importCommandPresets(const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError ||
        root.value(QStringLiteral("format")).toString() != QLatin1String("ludo-command-presets")) {
        if (error) *error = QStringLiteral("Arquivo de presets inválido."); return false;
    }
    QVector<CommandPreset> presets = loadCommandPresets();
    QSet<QString> ids; for (const CommandPreset& preset : presets) ids.insert(preset.id);
    for (const QJsonValue& value : root.value(QStringLiteral("presets")).toArray()) {
        if (presets.size() >= 256) break;
        CommandPreset preset = fromJson(value.toObject());
        if (!valid(preset)) continue;
        if (ids.contains(preset.id)) preset.id = core::idGen();
        ids.insert(preset.id); presets.push_back(preset);
    }
    persist(presets);
    return true;
}

} // namespace ui
