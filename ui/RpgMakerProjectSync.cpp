#include "RpgMakerPublication.h"
#include <QScopedValueRollback>
#include "RpgMakerProjectSync.h"

#include "RpgMakerExporter.h"
#include "RpgMakerMapVisualImport.h"
#include "RpgMakerMvReset.h"
#include "core/ProjectIO.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace ui {
namespace {

QByteArray hashBytes(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

bool isAllowedSyncWriteTarget(const QString& root, const QString& path)
{
    const QString dataDir = QDir::cleanPath(QDir(root).filePath(QStringLiteral("data")));
    const QFileInfo targetInfo(QDir::cleanPath(path));
    if (QString::compare(QDir::cleanPath(targetInfo.absolutePath()), dataDir, Qt::CaseInsensitive) != 0) return false;

    const QString fileName = targetInfo.fileName();
    if (fileName == QStringLiteral("MapInfos.json")) return true;
    static const QRegularExpression mapFileRe(QStringLiteral(R"(^Map\d{3,4}\.json$)"));
    return mapFileRe.match(fileName).hasMatch();
}

bool writeSyncFile(const QString& root, const QString& path, const QByteArray& bytes, QString* error)
{
    if (!isAllowedSyncWriteTarget(root, path)) {
        if (error) *error = QObject::tr(
            "A sincronização bloqueou uma tentativa de escrita fora de data/MapInfos.json ou data/MapXXX.json: %1")
            .arg(path);
        return false;
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error) *error = QObject::tr("Não foi possível gravar %1.").arg(path);
        return false;
    }
    return true;
}

bool backupBeforeSync(const QString& root, const QString& sourcePath, const QString& backupName, QString* error)
{
    QFile source(sourcePath);
    if (!source.exists()) return true;
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Não foi possível criar backup de %1.").arg(sourcePath);
        return false;
    }
    const QByteArray bytes = source.readAll();
    if (bytes.isEmpty()) {
        if (error) *error = QObject::tr("O arquivo %1 está vazio; a sincronização foi cancelada para não propagar corrupção.").arg(sourcePath);
        return false;
    }

    const QString dir = QDir(root).filePath(QStringLiteral("data/ludoMaps/backups/sync"));
    QDir().mkpath(dir);
    const QString backupPath = QDir(dir).filePath(backupName);
    QSaveFile out(backupPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error) *error = QObject::tr("Não foi possível gravar o backup de sincronização %1.").arg(backupPath);
        return false;
    }
    return true;
}

QJsonObject readJsonObject(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &e);
    return e.error == QJsonParseError::NoError && d.isObject() ? d.object() : QJsonObject();
}

QString mapFilePath(const QString& root, int id)
{
    return QDir(root).filePath(QStringLiteral("data/") + rpgMaker::mapJsonName(id));
}

} // namespace

RpgMakerProjectSync::RpgMakerProjectSync(core::Editor& editor, QWidget* owner, QObject* parent)
    : QObject(parent), ed(editor), m_owner(owner)
{
    m_externalDebounce.setSingleShot(true);
    m_externalDebounce.setInterval(250);
    m_structureDebounce.setSingleShot(true);
    m_structureDebounce.setInterval(180);

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        m_externalDebounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_externalDebounce.start();
    });
    connect(&m_externalDebounce, &QTimer::timeout, this, [this] { onExternalChange(); });
    connect(&m_structureDebounce, &QTimer::timeout, this, [this] {
        if (m_syncing || !isLinked()) return;
        if (structureSignature() == m_lastStructureSignature) return;
        QString error;
        if (!pushStructure(&error)) notifyStatus(tr("Sincronização %1: %2").arg(core::rpgMakerEngineName(ed.rpgMakerEngine), error));
    });

    // docsChanged também ocorre ao trocar a aba ativa; a assinatura estrutural
    // impede escritas desnecessárias nesse caso.
    connect(&ed, &core::Editor::docsChanged, this, [this] { scheduleStructurePush(); });
}

bool RpgMakerProjectSync::isLinked() const
{
    return rpgMaker::isRpgMakerProjectRoot(ed.rpgMakerProjectRoot, ed.rpgMakerEngine);
}

QString RpgMakerProjectSync::projectRoot() const
{
    return QDir::cleanPath(ed.rpgMakerProjectRoot);
}

void RpgMakerProjectSync::notifyStatus(const QString& text) const
{
    if (m_status) m_status(text);
}

void RpgMakerProjectSync::notifyModelChanged() const
{
    if (m_modelChanged) m_modelChanged();
}

QByteArray RpgMakerProjectSync::fileHash(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return hashBytes(file.readAll());
}

void RpgMakerProjectSync::refreshDiskBaseline()
{
    if (!isLinked()) {
        m_lastWrittenMapInfosHash.clear();
        m_seenMapHashes.clear();
        m_pendingExternalMapIds.clear();
        return;
    }

    const QDir root(projectRoot());
    m_lastWrittenMapInfosHash = fileHash(root.filePath(QStringLiteral("data/MapInfos.json")));
    QHash<int, QByteArray> hashes;
    for (const core::MapDoc& doc : std::as_const(ed.docs)) {
        if (doc.rpgMakerMapId <= 0) continue;
        const QByteArray hash = fileHash(mapFilePath(projectRoot(), doc.rpgMakerMapId));
        if (!hash.isEmpty()) hashes.insert(doc.rpgMakerMapId, hash);
    }
    m_seenMapHashes = hashes;
    m_pendingExternalMapIds.clear();
}

bool RpgMakerProjectSync::linkProjectInteractive()
{
    const QString initial = isLinked() ? projectRoot() : QDir::homePath();
    const QString engineName = core::rpgMakerEngineName(ed.rpgMakerEngine);
    const QString root = QFileDialog::getExistingDirectory(
        m_owner, tr("Vincular projeto %1").arg(engineName), initial);
    if (root.isEmpty()) return false;

    QString reason;
    if (!rpgMaker::isRpgMakerProjectRoot(root, ed.rpgMakerEngine, &reason)) {
        QMessageBox::warning(m_owner, tr("Projeto %1 inválido").arg(engineName), reason);
        return false;
    }

    const QString previousRoot = QDir::cleanPath(ed.rpgMakerProjectRoot);
    ed.rpgMakerProjectRoot = QDir::cleanPath(root);

    // Se o usuário apontar explicitamente para OUTRO projeto RPG Maker, IDs antigos
    // não são reutilizados às cegas. Isso evita que MAP001 de um projeto seja
    // tratado como MAP001 de outro e sobrescrito por engano.
    if (!previousRoot.isEmpty() &&
        QString::compare(previousRoot, ed.rpgMakerProjectRoot, Qt::CaseInsensitive) != 0) {
        for (core::MapDoc& d : ed.docs) {
            d.rpgMakerMapId = 0;
            d.rpgMakerImported = false;
        }
    }

    ed.projectDirty = true;
    emit ed.projectChanged();

    QString error;
    // Vínculo inicial é primeiro READ-ONLY: recebemos a árvore do RPG Maker antes de
    // decidir qualquer escrita. Só publicamos depois mapas locais ainda sem ID.
    if (!pullStructure(true, &error)) {
        QMessageBox::warning(m_owner, tr("Sincronização %1").arg(core::rpgMakerEngineName(ed.rpgMakerEngine)), error);
        return false;
    }
    bool hasUnboundLocalMap = false;
    for (const core::MapDoc& d : std::as_const(ed.docs)) {
        if (d.rpgMakerMapId <= 0 && !d.rpgMakerImported) { hasUnboundLocalMap = true; break; }
    }
    if (hasUnboundLocalMap && !pushStructure(&error)) {
        QMessageBox::warning(m_owner, tr("Sincronização %1").arg(core::rpgMakerEngineName(ed.rpgMakerEngine)), error);
        return false;
    }
    armWatcher();
    notifyStatus(tr("Projeto %1 vinculado. A árvore foi recebida com sincronização segura.").arg(engineName));
    return true;
}

bool RpgMakerProjectSync::restoreLinkedProject()
{
    if (m_syncing) return isLinked();
    m_watcher.removePaths(m_watcher.files());
    m_watcher.removePaths(m_watcher.directories());
    if (!isLinked()) return false;

    QString error;
    // Abrir o LUDO nunca deve regravar o projeto RPG Maker. No startup, a operação é
    // estritamente RPG Maker -> LUDO. Escritas só acontecem após mudança local real
    // ou comando explícito de salvar/sincronizar.
    if (!pullStructure(true, &error)) {
        notifyStatus(tr("Não foi possível ler a árvore do %1: %2").arg(core::rpgMakerEngineName(ed.rpgMakerEngine), error));
        return false;
    }
    armWatcher();
    return true;
}

bool RpgMakerProjectSync::ensureLinked(QString* error)
{
    if (isLinked()) return true;
    if (linkProjectInteractive()) return true;
    if (error) *error = tr("Nenhum projeto %1 está vinculado.").arg(core::rpgMakerEngineName(ed.rpgMakerEngine));
    return false;
}

void RpgMakerProjectSync::armWatcher()
{
    if (!isLinked()) return;
    const QString dataDir = QDir(projectRoot()).filePath(QStringLiteral("data"));
    const QString infos = QDir(dataDir).filePath(QStringLiteral("MapInfos.json"));
    if (QFileInfo::exists(dataDir) && !m_watcher.directories().contains(dataDir))
        m_watcher.addPath(dataDir);
    if (QFileInfo::exists(infos) && !m_watcher.files().contains(infos))
        m_watcher.addPath(infos);

    // MapInfos representa a árvore. Alterações de conteúdo, porém, podem
    // modificar apenas MapXXX.json (comum no MV). Observamos cada mapa
    // vinculado para a sincronização realmente ser bidirecional.
    for (const core::MapDoc& doc : std::as_const(ed.docs)) {
        if (doc.rpgMakerMapId <= 0) continue;
        const QString mapPath = mapFilePath(projectRoot(), doc.rpgMakerMapId);
        if (QFileInfo::exists(mapPath) && !m_watcher.files().contains(mapPath))
            m_watcher.addPath(mapPath);
    }
}

void RpgMakerProjectSync::onExternalChange()
{
    if (m_syncing || !isLinked()) { armWatcher(); return; }

    const QByteArray infosHash = fileHash(QDir(projectRoot()).filePath(QStringLiteral("data/MapInfos.json")));
    const bool structureChanged = !infosHash.isEmpty() && infosHash != m_lastWrittenMapInfosHash;

    m_pendingExternalMapIds.clear();
    for (const core::MapDoc& doc : std::as_const(ed.docs)) {
        if (doc.rpgMakerMapId <= 0) continue;
        const QByteArray current = fileHash(mapFilePath(projectRoot(), doc.rpgMakerMapId));
        const QByteArray previous = m_seenMapHashes.value(doc.rpgMakerMapId);
        if (!current.isEmpty() && !previous.isEmpty() && current != previous)
            m_pendingExternalMapIds.insert(doc.rpgMakerMapId);
    }

    if (!structureChanged && m_pendingExternalMapIds.isEmpty()) {
        armWatcher();
        return;
    }

    QString error;
    if (pullStructure(true, &error)) {
        notifyStatus(tr("Alterações de mapas do %1 recebidas.").arg(core::rpgMakerEngineName(ed.rpgMakerEngine)));
        notifyModelChanged();
    } else {
        notifyStatus(tr("Falha ao receber alterações do %1: %2").arg(core::rpgMakerEngineName(ed.rpgMakerEngine), error));
        m_pendingExternalMapIds.clear();
    }
    armWatcher();
}

bool RpgMakerProjectSync::loadMapInfos(QJsonArray& infos, QString* error) const
{
    if (!isLinked()) {
        if (error) *error = tr("Projeto %1 não vinculado.").arg(core::rpgMakerEngineName(ed.rpgMakerEngine));
        return false;
    }
    const QString path = QDir(projectRoot()).filePath(QStringLiteral("data/MapInfos.json"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("Não foi possível ler %1.").arg(path);
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error) *error = tr("MapInfos.json inválido: %1").arg(parse.errorString());
        return false;
    }
    infos = doc.array();
    return true;
}

bool RpgMakerProjectSync::writeMapInfos(const QJsonArray& infos, QString* error)
{
    const QString path = QDir(projectRoot()).filePath(QStringLiteral("data/MapInfos.json"));
    const QByteArray bytes = QJsonDocument(infos).toJson(QJsonDocument::Compact);

    // Nunca substitui MapInfos sem guardar a última versão válida. Esse backup
    // é atualizado antes de cada escrita estrutural e fica fora da pasta data
    // principal para não interferir no RPG Maker.
    if (!backupBeforeSync(projectRoot(), path, QStringLiteral("MapInfos.json.before-sync"), error))
        return false;

    const bool wasSyncing = m_syncing;
    m_syncing = true;
    const bool ok = writeSyncFile(projectRoot(), path, bytes, error);
    if (ok) m_lastWrittenMapInfosHash = hashBytes(bytes);
    m_syncing = wasSyncing;
    armWatcher();
    return ok;
}

QByteArray RpgMakerProjectSync::structureSignature() const
{
    QByteArray sig;
    for (const core::MapDoc& d : ed.docs) {
        sig += d.id.toUtf8(); sig += '|';
        sig += QByteArray::number(d.rpgMakerMapId); sig += '|';
        sig += d.name.toUtf8(); sig += '|';
        sig += d.parentId.toUtf8(); sig += '\n';
    }
    return hashBytes(sig);
}

int RpgMakerProjectSync::firstFreeMapId(const QJsonArray& infos, const QSet<int>& reserved) const
{
    for (int id = 1; id <= 9999; ++id) {
        if (reserved.contains(id)) continue;
        if (id >= infos.size() || !infos.at(id).isObject()) return id;
    }
    return 0;
}

void RpgMakerProjectSync::importRegions(core::MapDoc& doc, const QJsonObject& rpgMakerMap) const
{
    const int w = qMax(1, rpgMakerMap.value(QStringLiteral("width")).toInt(doc.map.width));
    const int h = qMax(1, rpgMakerMap.value(QStringLiteral("height")).toInt(doc.map.height));
    const QJsonArray data = rpgMakerMap.value(QStringLiteral("data")).toArray();
    const qint64 cells = qint64(w) * h;
    if (data.size() < cells * 6) return;
    doc.rpgMakerRegions.clear();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const qint64 index = (qint64(5) * h + y) * w + x;
            const int id = qBound(0, data.at(int(index)).toInt(), 255);
            if (id > 0) doc.rpgMakerRegions.insert(core::MapDoc::regionKey(x, y), quint8(id));
        }
    }
    doc.rpgMakerRegionsAuthored = true;
}

core::MapDoc RpgMakerProjectSync::importRpgMakerMap(int rpgMakerMapId, const QJsonObject& info, QString* error) const
{
    core::MapDoc map;
    map.rpgMakerMapId = rpgMakerMapId;
    map.rpgMakerImported = true;
    map.name = info.value(QStringLiteral("name")).toString(tr("Mapa %1").arg(rpgMakerMapId));

    const QString path = mapFilePath(projectRoot(), rpgMakerMapId);
    const QJsonObject rpgMakerMap = readJsonObject(path);
    map.map.width = qMax(1, rpgMakerMap.value(QStringLiteral("width")).toInt(40));
    map.map.height = qMax(1, rpgMakerMap.value(QStringLiteral("height")).toInt(30));
    const auto system = rpgMakerVisual::json(QDir(projectRoot()).filePath("data/System.json"));
    const int size = ed.rpgMakerEngine == core::RpgMakerEngine::MV
                         ? 48
                         : system.value("advanced").toObject().value("tileSize").toInt(48);
    map.map.tileWidth = map.map.tileHeight = qBound(16,size,256);
    const auto manifest = rpgMakerVisual::json(QDir(projectRoot()).filePath("data/ludoMaps/" + rpgMaker::mapJsonName(rpgMakerMapId)));
    QString sourceError;
    bool restored = rpgMakerVisual::restoreSource(ed,map,manifest.value("editorSource").toObject(),&sourceError);
    if (!restored) {
        const QDir folder(projectRoot());
        for (const QString& file : folder.entryList({"*.ludo"},QDir::Files,QDir::Name)) {
            if (rpgMakerVisual::restoreSource(ed,map,rpgMakerVisual::json(folder.filePath(file)),&sourceError)) {restored=true;break;}
        }
    }
    if (!restored) {
        QStringList missing;
        if (!manifest.isEmpty()) {
            const int fallbackTile = core::rpgMakerDefaultAuthoringTileSize(ed.rpgMakerEngine);
            map.map.tileWidth=qMax(1,manifest.value("tileWidth").toInt(fallbackTile));
            map.map.tileHeight=qMax(1,manifest.value("tileHeight").toInt(fallbackTile));
            rpgMakerVisual::importNative(map,projectRoot(),rpgMakerMap,missing);
            rpgMakerVisual::importRuntime(ed,map,projectRoot(),manifest,missing);
        } else {
            const QString panorama = rpgMakerMap.value("parallaxName").toString();
            if (!panorama.isEmpty()) {
                const QString source = rpgMakerVisual::asset(projectRoot(),"img/parallaxes",panorama+".png");
                QImage image(source);
                if (!image.isNull()) map.layers.push_back(core::makeImageLayer(image,"Panorama RPG Maker",source,true));
                else missing << source;
            }
            rpgMakerVisual::importNative(map,projectRoot(),rpgMakerMap,missing);
        }
        missing.removeDuplicates();
        if (!missing.isEmpty()) notifyStatus(tr("Mapa %1: recursos ausentes: %2").arg(rpgMakerMapId).arg(missing.join("; ")));
    }

    const int cols = map.map.width;
    const int rows = map.map.height;
    if (!restored) {
        map.layers.push_back(core::makeTileLayer(QStringLiteral("Chão"), map.map.tileWidth, map.map.tileHeight, cols, rows));
        map.layers.push_back(core::makeTileLayer(QStringLiteral("Decoração"), map.map.tileWidth, map.map.tileHeight, cols, rows));
        map.activeLayerIdx = map.layers.size()-1;
        map.activeLayerId = map.layers.last()->id;
    }
    map.dirty = false;
    importRegions(map, rpgMakerMap);
    Q_UNUSED(error);
    return map;
}

bool RpgMakerProjectSync::pullStructure(bool importNewMaps, QString* error)
{
    QJsonArray infos;
    if (!loadMapInfos(infos, error)) return false;
    m_syncing = true;

    QHash<int, QString> docIdByRpgMakerId;
    for (const core::MapDoc& d : std::as_const(ed.docs))
        if (d.rpgMakerMapId > 0) docIdByRpgMakerId.insert(d.rpgMakerMapId, d.id);

    bool changed = false;
    for (int id = 1; id < infos.size(); ++id) {
        if (!infos.at(id).isObject()) continue;
        const QJsonObject info = infos.at(id).toObject();
        core::MapDoc* existing = nullptr;
        for (core::MapDoc& d : ed.docs)
            if (d.rpgMakerMapId == id) { existing = &d; break; }

        if (!existing && importNewMaps) {
            // Projeto LUDO recém-criado contém um único mapa-base vazio. Ao
            // vincular um projeto RPG Maker já existente, reaproveitamos esse documento para
            // o primeiro mapa externo em vez de criar um "Mapa 1" duplicado.
            if (ed.docs.size() == 1 && ed.docs[0].rpgMakerMapId == 0 &&
                !ed.docs[0].dirty && ed.tilesets.isEmpty()) {
                const QString keepId = ed.docs[0].id;
                core::MapDoc imported = importRpgMakerMap(id, info, error);
                imported.id = keepId;
                ed.docs[0] = imported;
                existing = &ed.docs[0];
                docIdByRpgMakerId.insert(id, keepId);
                changed = true;
            }
            if (!existing) {
                core::MapDoc imported = importRpgMakerMap(id, info, error);
                docIdByRpgMakerId.insert(id, imported.id);
                ed.docs.push_back(imported);
                changed = true;
                continue;
            }
        }
        if (existing && !existing->dirty) {
            bool visual = false;
            std::function<void(const core::LayerPtr&)> inspect = [&](const core::LayerPtr& layer) {
                if (!layer->image.isNull() || !layer->objects.isEmpty()) visual = true;
                for (const auto& row : layer->data2D) for (const auto& cell : row) if (!cell.isEmpty()) visual = true;
                for (const auto& child : layer->children) inspect(child);
            };
            for (const auto& layer : existing->layers) inspect(layer);
            const bool externalMapChanged = m_pendingExternalMapIds.contains(id);
            if (existing->rpgMakerImported && (!visual || externalMapChanged)) {
                const QString keepId=existing->id, keepParent=existing->parentId;
                *existing=importRpgMakerMap(id,info,error);
                existing->id=keepId;existing->parentId=keepParent;changed=true;
            }
            const QString rpgMakerName = info.value(QStringLiteral("name")).toString().trimmed();
            if (!rpgMakerName.isEmpty() && existing->name != rpgMakerName) {
                existing->name = rpgMakerName;
                changed = true;
            }
        }
    }

    if (changed) { emit ed.tilesetsChanged(); emit ed.wangChanged(); }

    // Segunda passagem: parentId só pode ser resolvido depois que todos os
    // mapas externos foram materializados no LUDO.
    docIdByRpgMakerId.clear();
    for (const core::MapDoc& d : std::as_const(ed.docs))
        if (d.rpgMakerMapId > 0) docIdByRpgMakerId.insert(d.rpgMakerMapId, d.id);

    for (int id = 1; id < infos.size(); ++id) {
        if (!infos.at(id).isObject()) continue;
        core::MapDoc* d = nullptr;
        for (core::MapDoc& candidate : ed.docs)
            if (candidate.rpgMakerMapId == id) { d = &candidate; break; }
        if (!d || d->dirty) continue;
        const int parentRpgMakerId = infos.at(id).toObject().value(QStringLiteral("parentId")).toInt(0);
        const QString wantedParent = docIdByRpgMakerId.value(parentRpgMakerId);
        if (d->parentId != wantedParent) {
            d->parentId = wantedParent;
            changed = true;
        }
    }

    // Ordem dos irmãos segue MapInfos.order. Isso faz a árvore aparecer igual
    // nos dois editores sem transformar parentId em posição visual.
    QHash<int, int> orderById;
    for (int id = 1; id < infos.size(); ++id)
        if (infos.at(id).isObject()) orderById.insert(id, infos.at(id).toObject().value(QStringLiteral("order")).toInt(id));
    const QString activeId = ed.doc() ? ed.doc()->id : QString();
    QStringList beforeOrder;
    for (const core::MapDoc& d : std::as_const(ed.docs)) beforeOrder.push_back(d.id);
    std::stable_sort(ed.docs.begin(), ed.docs.end(), [&](const core::MapDoc& a, const core::MapDoc& b) {
        return orderById.value(a.rpgMakerMapId, 100000 + qMax(0, a.rpgMakerMapId)) <
               orderById.value(b.rpgMakerMapId, 100000 + qMax(0, b.rpgMakerMapId));
    });
    QStringList afterOrder;
    for (const core::MapDoc& d : std::as_const(ed.docs)) afterOrder.push_back(d.id);
    if (beforeOrder != afterOrder) changed = true;
    if (!activeId.isEmpty()) {
        const int active = ed.mapIndexById(activeId);
        if (active >= 0) ed.activeDocIdx = active;
    }

    if (changed) {
        ed.projectDirty = true;
        if (ed.activeDocIdx < 0 || ed.activeDocIdx >= ed.docs.size()) ed.activeDocIdx = ed.docs.isEmpty() ? -1 : 0;
        emit ed.docsChanged();
        emit ed.layersChanged();
        emit ed.mapChanged();
        emit ed.projectChanged();
    }

    m_syncing = false;
    m_lastStructureSignature = structureSignature();
    refreshDiskBaseline();
    return true;
}

bool RpgMakerProjectSync::pushStructure(QString* error)
{
    if (!isLinked()) {
        if (error) *error = tr("Projeto %1 não vinculado.").arg(core::rpgMakerEngineName(ed.rpgMakerEngine));
        return false;
    }

    // Defesa principal: o sincronizador só escreve MapInfos/MapXXX e nunca
    // acessa a configuração de plugins do projeto RPG Maker.

    QJsonArray infos;
    if (!loadMapInfos(infos, error)) return false;
    m_syncing = true;

    QSet<int> reserved;
    QSet<int> newlyAssigned;
    for (int id = 1; id < infos.size(); ++id)
        if (infos.at(id).isObject()) reserved.insert(id);

    // Corrige vínculos duplicados de builds experimentais sem sobrescrever um
    // MapXXX já pertencente a outro documento. O primeiro vínculo permanece.
    QSet<int> boundOnce;
    bool assigned = false;
    for (core::MapDoc& d : ed.docs) {
        if (d.rpgMakerMapId <= 0) continue;
        if (boundOnce.contains(d.rpgMakerMapId)) {
            d.rpgMakerMapId = 0;
            assigned = true;
        } else {
            boundOnce.insert(d.rpgMakerMapId);
            reserved.insert(d.rpgMakerMapId);
        }
    }

    for (core::MapDoc& d : ed.docs) {
        if (d.rpgMakerMapId > 0) continue;
        const int id = firstFreeMapId(infos, reserved);
        if (id <= 0) {
            m_syncing = false;
            if (error) *error = tr("Não há Map ID livre entre 1 e 9999.");
            return false;
        }
        d.rpgMakerMapId = id;
        d.dirty = true; // mapa recém-vinculado precisa gerar o manifesto/runtime completo
        reserved.insert(id);
        newlyAssigned.insert(id);
        assigned = true;
    }

    // Se uma variação acabou de receber Map ID, todos os estados do mesmo
    // cenário precisam ser reexportados para compartilhar o índice completo.
    QSet<QString> variationScenariosToRefresh;
    for (const core::MapDoc& d : std::as_const(ed.docs)) {
        if (!newlyAssigned.contains(d.rpgMakerMapId)) continue;
        if (!d.variationBaseId.isEmpty()) variationScenariosToRefresh.insert(d.variationBaseId);
        else {
            for (const core::MapDoc& candidate : std::as_const(ed.docs))
                if (candidate.variationBaseId == d.id) { variationScenariosToRefresh.insert(d.id); break; }
        }
    }
    for (core::MapDoc& d : ed.docs)
        for (const QString& baseId : std::as_const(variationScenariosToRefresh))
            if (d.id == baseId || d.variationBaseId == baseId) d.dirty = true;

    QHash<QString, int> rpgMakerIdByDoc;
    for (const core::MapDoc& d : std::as_const(ed.docs)) rpgMakerIdByDoc.insert(d.id, d.rpgMakerMapId);

    int nextOrder = 1;

    for (core::MapDoc& d : ed.docs) {
        const int id = d.rpgMakerMapId;
        while (infos.size() <= id) infos.append(QJsonValue(QJsonValue::Null));
        const bool existed = infos.at(id).isObject();
        QJsonObject info = existed ? infos.at(id).toObject() : QJsonObject();
        info.insert(QStringLiteral("id"), id);
        info.insert(QStringLiteral("name"), d.name.trimmed().isEmpty() ? tr("Mapa %1").arg(id) : d.name.trimmed());
        info.insert(QStringLiteral("parentId"), rpgMakerIdByDoc.value(d.parentId, 0));
        info.insert(QStringLiteral("order"), nextOrder++);
        if (!info.contains(QStringLiteral("expanded"))) info.insert(QStringLiteral("expanded"), false);
        if (!info.contains(QStringLiteral("scrollX"))) info.insert(QStringLiteral("scrollX"), 0);
        if (!info.contains(QStringLiteral("scrollY"))) info.insert(QStringLiteral("scrollY"), 0);
        infos[id] = info;

        const QString mapPath = mapFilePath(projectRoot(), id);
        if (!QFileInfo::exists(mapPath) && (newlyAssigned.contains(id) || !existed)) {
            QJsonObject rpgMakerMap = rpgMaker::makeDefaultRpgMakerMap(d);
            QJsonArray data;
            const qint64 total = qint64(d.map.width) * d.map.height * 6;
            for (qint64 i = 0; i < total; ++i) data.append(0);
            rpgMakerMap.insert(QStringLiteral("data"), data);
            QString mapError;
            if (!writeSyncFile(projectRoot(), mapPath, QJsonDocument(rpgMakerMap).toJson(QJsonDocument::Compact), &mapError)) {
                m_syncing = false;
                if (error) *error = mapError;
                return false;
            }
        }
    }

    if (!writeMapInfos(infos, error)) {
        m_syncing = false;
        return false;
    }

    if (assigned) {
        ed.projectDirty = true;
        emit ed.docsChanged();
        emit ed.projectChanged();
    }
    m_lastStructureSignature = structureSignature();
    m_syncing = false;
    refreshDiskBaseline();
    armWatcher();
    return true;
}

bool RpgMakerProjectSync::synchronizeNow(bool importNewMaps)
{
    QString error;
    if (!ensureLinked(&error)) return false;
    if (!pullStructure(importNewMaps, &error) || !pushStructure(&error)) {
        QMessageBox::warning(m_owner, tr("Sincronização %1").arg(core::rpgMakerEngineName(ed.rpgMakerEngine)), error);
        return false;
    }
    notifyModelChanged();
    notifyStatus(ed.rpgMakerEngine == core::RpgMakerEngine::MV
                     ? tr("Projeto MV sincronizado em disco. Se o RPG Maker MV estiver aberto, reabra o projeto nele para carregar as alterações externas.")
                     : tr("Árvore de mapas sincronizada nos dois sentidos com proteção não destrutiva."));
    return true;
}

void RpgMakerProjectSync::scheduleStructurePush()
{
    if (!m_syncing && isLinked()) m_structureDebounce.start();
}

bool RpgMakerProjectSync::persistProjectContainer(QString* error)
{
    if (ed.projectPath.isEmpty()) {
        if (error) *error = tr("O projeto LUDO ainda não possui arquivo .ludo.");
        return false;
    }
    return core::io::saveProject(ed, ed.projectPath, error);
}

void RpgMakerProjectSync::publishProject(CollaborationClient* team)
{
    if(m_syncing)return;
    {
        QScopedValueRollback<bool> guard(m_syncing,true);
        m_structureDebounce.stop();m_externalDebounce.stop();
        runRpgMakerPublication(ed,m_owner,team);
    }
    m_lastStructureSignature=structureSignature();
    refreshDiskBaseline();
    armWatcher();
}


bool RpgMakerProjectSync::saveAllDirtyMaps()
{
    QString error;
    if (!ensureLinked(&error)) return false;
    if (!pushStructure(&error)) {
        QMessageBox::warning(m_owner, tr("Salvar mapas"), error);
        return false;
    }
    const QString fitKey = QStringLiteral("LudoRpgMaker/%1/referenceFitGrid").arg(core::rpgMakerEngineId(ed.rpgMakerEngine));
    const bool fitGrid = QSettings().value(fitKey, false).toBool();
    bool exportedAny = false;
    m_syncing = true;
    for (const core::MapDoc& d : std::as_const(ed.docs)) {
        // O pipeline unificado salva primeiro o contêiner .ludo, que limpa os
        // flags dirty. O vínculo RPG Maker usa o estado confirmado inteiro;
        // o exportador atômico evita arquivos parcialmente atualizados.
        if (d.rpgMakerMapId <= 0) continue;
        if (!rpgMaker::exportBoundMap(ed, d, projectRoot(), d.rpgMakerMapId, fitGrid, m_owner, false)) {
            m_syncing = false;
            return false;
        }
        exportedAny = true;
    }
    m_syncing = false;
    refreshDiskBaseline();
    armWatcher();
    if (!persistProjectContainer(&error)) {
        QMessageBox::warning(m_owner, tr("Salvar mapas"), error);
        return false;
    }
    notifyStatus(ed.rpgMakerEngine == core::RpgMakerEngine::MV
                     ? tr("Todos os mapas foram gravados no projeto MV.")
                     : tr("Todos os mapas alterados foram salvos e sincronizados."));
    notifyModelChanged();
    if (exportedAny) rpgMakerMvReset::askAndReset(m_owner, ed.rpgMakerEngine, projectRoot());
    return true;
}

bool RpgMakerProjectSync::deleteMaps(const QVector<int>& rpgMakerMapIds, QString* error)
{
    if (!isLinked() || rpgMakerMapIds.isEmpty()) return true;
    QJsonArray infos;
    if (!loadMapInfos(infos, error)) return false;

    const QString backupDir = QDir(projectRoot()).filePath(QStringLiteral("data/ludoMaps/backups/deleted"));
    QDir().mkpath(backupDir);
    for (int id : rpgMakerMapIds) {
        if (id <= 0) continue;
        if (id < infos.size()) infos[id] = QJsonValue(QJsonValue::Null);
        const QString path = mapFilePath(projectRoot(), id);
        if (QFileInfo::exists(path)) {
            const QString backup = QDir(backupDir).filePath(
                rpgMaker::mapJsonName(id) + QStringLiteral(".before-delete"));
            if (!QFileInfo::exists(backup)) QFile::copy(path, backup);
            QFile::remove(path);
        }
    }
    if (!writeMapInfos(infos, error)) return false;
    refreshDiskBaseline();
    armWatcher();
    notifyStatus(tr("Mapa(s) removido(s) também da árvore do %1.").arg(core::rpgMakerEngineName(ed.rpgMakerEngine)));
    return true;
}

} // namespace ui
