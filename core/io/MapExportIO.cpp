// ============================================================================
// MapExportIO.cpp — saídas auxiliares do LUDO Map Editor.
//
// Fase 4: foram removidos os exportadores genéricos da antiga LUDO Engine
// (TMJ/Tiled, ZIP ★ e pacotes reduzidos). Os destinos oficiais são RPG Maker MV e RPG Maker MZ;
// aqui ficam apenas PNG de referência, imagem de tileset e presets Wang.
// ============================================================================
#include "core/ProjectIO.h"
#include "core/Renderer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
#include <QObject>
#include <algorithm>

namespace core { namespace io {

bool exportPNG(const Editor& ed, const QString& path, QString* error)
{
    RenderOptions opt;
    opt.skipReferenceLayers = true;     // igual ao _refLayerExportSkip do JS
    opt.drawObjectFrames = false;
    const QImage img = renderMapToImage(ed, opt);
    if (!img.save(path, "PNG")) {
        if (error) *error = QObject::tr("Falha ao gravar o PNG em %1.").arg(path);
        return false;
    }
    return true;
}

bool exportTilesetImage(const Editor& ed, int tilesetIdx, const QString& path, QString* error)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || ts->image.isNull()) {
        if (error) *error = QObject::tr("Tileset inválido ou sem imagem.");
        return false;
    }
    if (!ts->image.save(path, "PNG")) {
        if (error) *error = QObject::tr("Falha ao gravar %1.").arg(path);
        return false;
    }
    return true;
}

// ------------------------------------------------------------ presets Wang
/// Serializa uma lista de presets no formato da ferramenta web (array puro).
static QJsonArray presetsToJson(const QVector<WangPreset>& presets, bool includeBuiltin)
{
    QJsonArray arr;
    for (const WangPreset& p : presets) {
        if (p.builtin && !includeBuiltin) continue;
        QJsonObject o;
        o["id"] = p.id;
        o["name"] = p.name;
        o["type"] = p.type;
        o["w"] = p.w;
        o["h"] = p.h;
        QJsonObject pos;
        for (auto it = p.positions.constBegin(); it != p.positions.constEnd(); ++it) {
            QJsonObject flags;
            for (const QString& s : it.value()) flags.insert(s, true);
            pos.insert(it.key(), flags);
        }
        o["positions"] = pos;
        arr.append(o);
    }
    return arr;
}

/// Extrai a lista de presets de qualquer uma das formas conhecidas de arquivo.
static QJsonArray presetArrayFrom(const QJsonDocument& doc)
{
    if (doc.isArray()) return doc.array();
    if (!doc.isObject()) return QJsonArray();
    const QJsonObject root = doc.object();
    for (const char* key : { "presets", "wangPresets", "items", "data" })
        if (root.value(QLatin1String(key)).isArray()) return root.value(QLatin1String(key)).toArray();
    // Objeto único de preset (o "exportSingleWangPreset" da versão web).
    if (root.contains("positions")) { QJsonArray a; a.append(root); return a; }
    return QJsonArray();
}

/// Converte um objeto JSON num WangPreset. Devolve false se não for um preset.
static bool presetFromJson(const QJsonObject& o, WangPreset* out)
{
    const QJsonValue posVal = o.value("positions");
    if (!posVal.isObject()) return false;
    const QJsonObject pos = posVal.toObject();
    if (pos.isEmpty()) return false;

    WangPreset p;
    p.id = o.value("id").toString(idGen());
    p.name = o.value("name").toString(QObject::tr("Preset importado"));
    p.type = o.value("type").toString(QStringLiteral("mixed"));
    p.builtin = false;

    int maxX = 0, maxY = 0;
    for (auto it = pos.constBegin(); it != pos.constEnd(); ++it) {
        const QStringList xy = it.key().split(QLatin1Char(','));
        if (xy.size() != 2) continue;
        maxX = qMax(maxX, xy[0].toInt());
        maxY = qMax(maxY, xy[1].toInt());

        QSet<QString> flags;
        if (it.value().isObject()) {                 // {"tl":true,"t":true}
            const QJsonObject f = it.value().toObject();
            for (auto jt = f.constBegin(); jt != f.constEnd(); ++jt)
                if (jt.value().toBool()) flags.insert(jt.key());
        } else if (it.value().isArray()) {           // ["tl","t"]
            for (const QJsonValue& v : it.value().toArray())
                if (v.isString()) flags.insert(v.toString());
        }
        if (!flags.isEmpty()) p.positions.insert(it.key(), flags);
    }
    if (p.positions.isEmpty()) return false;

    // w/h podem faltar em arquivos escritos à mão: deduzimos das posições.
    p.w = qMax(1, o.value("w").toInt(maxX + 1));
    p.h = qMax(1, o.value("h").toInt(maxY + 1));
    *out = p;
    return true;
}

bool exportWangPresets(const Editor& ed, const QString& path, QString* error, int* count)
{
    const QJsonArray arr = presetsToJson(ed.wangPresets, false);
    if (count) *count = arr.size();
    if (arr.isEmpty()) {
        if (error) *error = QObject::tr(
            "Nenhum preset customizado para exportar.\n\n"
            "O “Blob 47 clássico (8×6)” é embutido e já vem pronto no programa, "
            "por isso não entra no arquivo.\n\n"
            "Para criar um preset seu: rotule os tiles de um bloco na paleta "
            "(aba Wang, com “Modo de edição Wang” ligado), selecione esse bloco "
            "na paleta e clique em “Salvar seleção como preset”.");
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QObject::tr("Não foi possível escrever em %1.").arg(path);
        return false;
    }
    // Array puro, igual ao JSON.stringify(custom, null, 2) da versão web.
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool importWangPresets(Editor& ed, const QString& path, QString* error, int* count)
{
    if (count) *count = 0;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Não foi possível abrir %1.").arg(path);
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (error) *error = QObject::tr("Arquivo JSON malformado: %1 (posição %2).")
                                .arg(perr.errorString()).arg(perr.offset);
        return false;
    }

    const QJsonArray arr = presetArrayFrom(doc);
    if (arr.isEmpty()) {
        // Mensagens específicas em vez de um "arquivo inválido" genérico.
        if (doc.isObject() && doc.object().contains("presets")) {
            if (error) *error = QObject::tr(
                "O arquivo tem a estrutura certa, mas a lista de presets está VAZIA.\n\n"
                "Isso acontece quando ele foi exportado sem nenhum preset customizado "
                "(o Blob 47 embutido não é exportado). Crie um preset primeiro com "
                "“Salvar seleção como preset”, na aba Wang.");
        } else if (doc.isObject() && doc.object().contains("wangSets")) {
            if (error) *error = QObject::tr(
                "Este arquivo é um PROJETO (.json), não um arquivo de presets Wang.\n\n"
                "Para abrir um projeto use Arquivo ▸ Abrir projeto.");
        } else {
            if (error) *error = QObject::tr(
                "Não encontrei presets neste arquivo.\n\n"
                "Esperado: uma lista de objetos com os campos “name”, “w”, “h” e "
                "“positions” — o mesmo formato exportado por este programa e pela "
                "versão web da ferramenta.");
        }
        return false;
    }

    QSet<QString> existing;
    for (const WangPreset& p : ed.wangPresets) existing.insert(p.id);

    int imported = 0, skipped = 0;
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) { ++skipped; continue; }
        WangPreset p;
        if (!presetFromJson(v.toObject(), &p)) { ++skipped; continue; }
        if (existing.contains(p.id)) p.id = idGen();   // evita id duplicado
        existing.insert(p.id);
        ed.wangPresets.push_back(p);
        ++imported;
    }

    if (!imported) {
        if (error) *error = QObject::tr(
            "O arquivo contém %1 entradas, mas nenhuma é um preset válido "
            "(falta o campo “positions” com os rótulos dos tiles).").arg(arr.size());
        return false;
    }
    if (count) *count = imported;
    if (skipped && error)
        *error = QObject::tr("Entradas ignoradas por não serem presets: %1.").arg(skipped);

    saveWangPresetsToSettings(ed);
    emit ed.wangChanged();
    return true;
}

void saveWangPresetsToSettings(const Editor& ed)
{
    QSettings s;
    const QJsonArray arr = presetsToJson(ed.wangPresets, false);
    s.setValue(QStringLiteral("wangPresets"),
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void loadWangPresetsFromSettings(Editor& ed)
{
    QSettings s;
    const QString raw = s.value(QStringLiteral("wangPresets")).toString();
    if (raw.isEmpty()) return;
    QSet<QString> existing;
    for (const WangPreset& preset : ed.wangPresets)
        if (!preset.id.isEmpty()) existing.insert(preset.id);
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    for (const QJsonValue& v : presetArrayFrom(doc)) {
        WangPreset p;
        if (!presetFromJson(v.toObject(), &p) || existing.contains(p.id)) continue;
        existing.insert(p.id);
        ed.wangPresets.push_back(p);
    }
}

}} // namespace core::io
