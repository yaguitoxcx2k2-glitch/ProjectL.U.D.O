#include "RuntimePreloader.h"

#include "core/AssetDatabase.h"
#include "core/Editor.h"
#include "core/ProjectIO.h"
#include "core/RuntimePreloadCache.h"
#include "core/RuntimeProjectSnapshot.h"
#include "game/GameWorld.h"
#include "game/RuntimeShaderCache.h"
#include "game/RuntimeAudioPolicy.h"
#include "game/TextPicture.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>

namespace game {
namespace {

struct AssetTask {
    QString id;
    QString path;
    QString absolutePath;
    QString type;
    QString owner;
    qint64 size = 1;
    qint64 workUnits = 1;
};

bool emitProgress(const RuntimePreloadProgressCallback& callback,
                  const QString& stage, const QString& detail,
                  qint64 completed, qint64 total, int item, int itemCount)
{
    if (!callback) return true;
    return callback(RuntimePreloadProgress{stage, detail, completed, total, item, itemCount});
}

QString absoluteProjectPath(const core::Editor& editor, const QString& path)
{
    if (path.trimmed().isEmpty()) return {};
    const QFileInfo info(path);
    return QDir::cleanPath(info.isAbsolute() ? info.absoluteFilePath()
                                             : QDir(editor.projectRoot()).filePath(path));
}

bool ownerBelongsToMap(const QString& owner, int mapIndex, bool* isMapOwned)
{
    if (isMapOwned) *isMapOwned = false;
    if (!owner.startsWith(QStringLiteral("/maps/"))) return false;
    const QString tail = owner.mid(6);
    const int slash = tail.indexOf(QLatin1Char('/'));
    bool ok = false;
    const int index = (slash < 0 ? tail : tail.left(slash)).toInt(&ok);
    if (!ok) return false;
    if (isMapOwned) *isMapOwned = true;
    return index == mapIndex;
}


QVector<AssetTask> collectAssetTasks(const core::Editor& editor,
                                     const RuntimePreloadOptions& options,
                                     int* requestedMapOut = nullptr)
{
    int requestedMap = options.mapId.isEmpty() ? editor.activeDocIdx
                                                : editor.mapIndexById(options.mapId);
    if (requestedMap < 0 || requestedMap >= editor.docs.size())
        requestedMap = editor.activeDocIdx;
    if (requestedMapOut) *requestedMapOut = requestedMap;

    const QJsonArray refs = core::io::buildRuntimeProjectPayload(editor)
                                .value(QStringLiteral("assetReferences")).toArray();
    QVector<AssetTask> tasks;
    QSet<QString> seen;
    for (const QJsonValue& value : refs) {
        const QJsonObject ref = value.toObject();
        AssetTask task;
        task.id = ref.value(QStringLiteral("id")).toString().trimmed();
        task.path = core::AssetDatabase::normalizePath(ref.value(QStringLiteral("path")).toString());
        task.owner = ref.value(QStringLiteral("owner")).toString();
        task.type = ref.value(QStringLiteral("type")).toString();
        if (task.path.isEmpty()) continue;
        if (options.scope == RuntimePreloadScope::CurrentMap) {
            bool mapOwned = false;
            const bool belongs = ownerBelongsToMap(task.owner, requestedMap, &mapOwned);
            if (mapOwned && !belongs) continue;
        }
        const QString key = !task.id.isEmpty() ? task.id : task.path.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        task.absolutePath = absoluteProjectPath(editor, task.path);
        const QFileInfo info(task.absolutePath);
        task.size = qMax<qint64>(1, info.exists() ? info.size() : 1);
        if (task.type.isEmpty()) task.type = core::AssetDatabase::typeForPath(task.path);
        task.workUnits = qMax<qint64>(1, task.size);
        tasks.push_back(task);
    }
    return tasks;
}

void addEmbeddedImage(const std::shared_ptr<core::RuntimePreloadCache>& cache,
                      const core::Editor& editor, const QString& path, const QImage& image)
{
    if (!cache || path.trimmed().isEmpty() || image.isNull()) return;
    cache->insertImage(editor.projectRoot(), path, image); // QImage COW: sem duplicar pixels
}

void addLayerEmbeddedImages(const core::LayerPtr& layer,
                            const std::shared_ptr<core::RuntimePreloadCache>& cache,
                            const core::Editor& editor)
{
    if (!layer) return;
    addEmbeddedImage(cache, editor, layer->imagePath, layer->image);
    for (const core::LayerPtr& child : layer->children)
        addLayerEmbeddedImages(child, cache, editor);
}

void registerEmbeddedImages(core::Editor& runtime,
                            const std::shared_ptr<core::RuntimePreloadCache>& cache,
                            const QVector<int>& mapIndexes)
{
    for (const core::Tileset& tileset : runtime.tilesets)
        addEmbeddedImage(cache, runtime, tileset.sourcePath, tileset.image);
    addEmbeddedImage(cache, runtime, runtime.player.charsetPath, runtime.player.charset);
    addEmbeddedImage(cache, runtime, runtime.iconSet.sourcePath, runtime.iconSet.image);
    for (const core::PictureAsset& picture : runtime.pictures)
        addEmbeddedImage(cache, runtime, picture.sourcePath, picture.image);

    for (int mapIndex : mapIndexes) {
        if (mapIndex < 0 || mapIndex >= runtime.docs.size()) continue;
        const core::MapDoc& map = runtime.docs.at(mapIndex);
        for (const core::LayerPtr& layer : map.layers)
            addLayerEmbeddedImages(layer, cache, runtime);
        addEmbeddedImage(cache, runtime, map.environment.panoramaPath, map.environment.panorama);
        for (const core::PanoramaDef& panorama : map.environment.panoramas)
            addEmbeddedImage(cache, runtime, panorama.sourcePath, panorama.image);
        for (const core::FogDef& fog : map.environment.fogs)
            addEmbeddedImage(cache, runtime, fog.sourcePath, fog.image);
        for (const core::MapEvent& event : map.events)
            for (const core::EventPage& page : event.pages)
                addEmbeddedImage(cache, runtime, page.graphic.sourcePath, page.graphic.charset);
    }
}

bool warmFileBytes(const AssetTask& task,
                   const std::shared_ptr<core::RuntimePreloadCache>& cache,
                   qint64* completed, qint64 total,
                   int item, int itemCount,
                   const RuntimePreloadProgressCallback& callback,
                   QString* warning)
{
    QFile file(task.absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (warning) *warning = QStringLiteral("Não foi possível aquecer %1").arg(task.path);
        *completed += qMax<qint64>(1, task.workUnits);
        return emitProgress(callback, QStringLiteral("Arquivo não encontrado"), task.path,
                            *completed, total, item, itemCount);
        // Runtime/Validator decide se isso é fatal; o preload só conclui a tarefa real de verificação.
    }
    constexpr qint64 chunkSize = 512 * 1024;
    qint64 read = 0;
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(chunkSize);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) {
            if (warning) *warning = QStringLiteral("Falha ao ler %1 durante o preload").arg(task.path);
            break;
        }
        read += bytes.size();
        const qint64 delta = bytes.size();
        *completed += delta;
        if (!emitProgress(callback, QStringLiteral("Preparando arquivos"), task.path,
                          *completed, total, item, itemCount)) return false;
    }
    // Arquivo vazio ainda consome a unidade mínima reservada no plano.
    const qint64 planned = qMax<qint64>(1, task.workUnits);
    if (read < planned) {
        *completed += planned - read;
        if (!emitProgress(callback, QStringLiteral("Preparando arquivos"), task.path,
                          *completed, total, item, itemCount)) return false;
    }
    cache->markWarmedFile(task.absolutePath);
    return true;
}

bool preloadAudioFile(const core::Editor& runtime, const AssetTask& task,
                      const std::shared_ptr<core::RuntimePreloadCache>& cache,
                      qint64* completed, qint64 total, int item, int itemCount,
                      const RuntimePreloadProgressCallback& callback,
                      RuntimePreloadReport* report, QString* warning)
{
    const QFileInfo info(task.absolutePath);
    if (!info.exists() || !info.isFile()) {
        if (warning) *warning = QStringLiteral("Áudio referenciado não existe: %1").arg(task.path);
        *completed += qMax<qint64>(1, task.workUnits);
        return emitProgress(callback, QStringLiteral("Áudio indisponível"), task.path,
                            *completed, total, item, itemCount);
    }

    const bool cacheBytes = audio_policy::shouldCacheAudioBytes(
        info.size(), cache ? cache->audioDataBytes() : 0);
    if (!cacheBytes) {
        const bool ok = warmFileBytes(task, cache, completed, total, item, itemCount,
                                      callback, warning);
        if (ok && report) ++report->audioFilesWarmed;
        return ok;
    }

    QFile file(task.absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (warning) *warning = QStringLiteral("Não foi possível carregar áudio: %1").arg(task.path);
        *completed += qMax<qint64>(1, task.workUnits);
        return emitProgress(callback, QStringLiteral("Áudio indisponível"), task.path,
                            *completed, total, item, itemCount);
    }

    QByteArray bytes;
    bytes.reserve(int(qMin<qint64>(info.size(), audio_policy::AudioPreloadPerFileBytes)));
    constexpr qint64 chunkSize = 512 * 1024;
    qint64 read = 0;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (warning) *warning = QStringLiteral("Falha ao carregar áudio durante preload: %1").arg(task.path);
            break;
        }
        bytes.append(chunk);
        read += chunk.size();
        *completed += chunk.size();
        if (!emitProgress(callback, QStringLiteral("Carregando áudio"), task.path,
                          *completed, total, item, itemCount)) return false;
    }
    const qint64 planned = qMax<qint64>(1, task.workUnits);
    if (read < planned) {
        *completed += planned - read;
        if (!emitProgress(callback, QStringLiteral("Carregando áudio"), task.path,
                          *completed, total, item, itemCount)) return false;
    }
    if (!bytes.isEmpty() && cache) {
        cache->insertAudioData(runtime.projectRoot(), task.path, bytes);
        cache->markWarmedFile(task.absolutePath);
        if (report) {
            ++report->audioFilesWarmed;
            ++report->audioFilesCached;
            report->audioBytesCached += bytes.size();
        }
    }
    return true;
}


void collectCommandTypes(const QJsonValue& value, QSet<QString>* out)
{
    if (!out) return;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& child : array) collectCommandTypes(child, out);
        return;
    }
    if (!value.isObject()) return;
    const QJsonObject object = value.toObject();
    // EventCommand serializado sempre carrega `type` + `params`. Exigir os
    // dois evita confundir assetReferences (que também possuem `type`) com
    // features executáveis do Runtime.
    const QString type = object.value(QStringLiteral("type")).toString().trimmed();
    if (!type.isEmpty() && object.contains(QStringLiteral("params"))) out->insert(type);
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        collectCommandTypes(it.value(), out);
}

void planRuntimeFeatures(const core::Editor& runtime,
                         const std::shared_ptr<core::RuntimePreloadCache>& cache)
{
    if (!cache) return;
    const QJsonObject payload = core::io::buildRuntimeProjectPayload(runtime);
    QSet<QString> commandTypes;
    collectCommandTypes(payload, &commandTypes);
    bool hasFilterSystem = false;
    bool hasComposedSampling = false;
    bool hasDynamicPluginCall = false;
    for (const QString& type : commandTypes) {
        cache->requestFeature(core::RuntimePreloadFeatures::command(type));
        if (type == QLatin1String("plugin.call")) hasDynamicPluginCall = true;
        if (!type.startsWith(QLatin1String("ludo.filter."))) continue;
        hasFilterSystem = true;
        if (type == QLatin1String("ludo.filter.blur") ||
            type == QLatin1String("ludo.filter.tiltShift") ||
            type == QLatin1String("ludo.filter.chromaticAberration"))
            hasComposedSampling = true;
    }
    // Plugins podem ativar efeitos em tempo de execução sem um comando
    // ludo.filter.* literal no projeto. Nesse caso mantemos o caminho
    // conservador e preparamos os pipelines de filtro.
    if (hasFilterSystem || hasDynamicPluginCall)
        cache->requestFeature(core::RuntimePreloadFeatures::filterSystem());
    if (hasComposedSampling) cache->requestFeature(core::RuntimePreloadFeatures::filterComposedSampling());
}


} // namespace

QByteArray runtimeProjectDigest(const core::Editor& editor)
{
    const QJsonObject payload = core::io::buildRuntimeProjectPayload(editor);
    return QCryptographicHash::hash(
        QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}

bool runtimePreloadCacheMatches(
    const core::Editor& editor,
    const std::shared_ptr<core::RuntimePreloadCache>& cache)
{
    return cache && !cache->projectDigest().isEmpty() &&
           cache->projectDigest() == runtimeProjectDigest(editor);
}

bool prepareRuntimeAudioAssets(core::Editor& runtime, const RuntimePreloadOptions& options,
                               RuntimePreloadReport* report, QString* error,
                               const RuntimePreloadProgressCallback& progress)
{
    if (error) error->clear();
    RuntimePreloadReport localReport;
    QElapsedTimer elapsed;
    elapsed.start();

    std::shared_ptr<core::RuntimePreloadCache> cache = runtime.runtimePreloadCache();
    if (!runtimePreloadCacheMatches(runtime, cache)) {
        cache = std::make_shared<core::RuntimePreloadCache>();
        cache->setProjectDigest(runtimeProjectDigest(runtime));
        runtime.setRuntimePreloadCache(cache);
    }

    // O Player standalone chega aqui antes de criar o QRhiWidget. Aproveitamos
    // o mesmo RuntimePreloader para descobrir features e desserializar shaders,
    // deixando somente a criação de QRhiGraphicsPipeline para o dispositivo real.
    planRuntimeFeatures(runtime, cache);
    localReport.requestedFeatures = cache->requestedFeatures();
    QStringList shaderErrors;
    localReport.shadersWarmed = warmRuntimeShaders(&shaderErrors);
    cache->setShadersWarmed(localReport.shadersWarmed);
    if (localReport.shadersWarmed) {
        cache->markFeaturePrepared(QStringLiteral("runtime-shaders"));
        if (cache->requestsFeature(core::RuntimePreloadFeatures::filterSystem()))
            cache->markFeaturePrepared(core::RuntimePreloadFeatures::filterSystem());
        if (cache->requestsFeature(core::RuntimePreloadFeatures::filterComposedSampling()))
            cache->markFeaturePrepared(core::RuntimePreloadFeatures::filterComposedSampling());
    } else if (error) {
        *error = QStringLiteral("Não foi possível preparar o renderizador gráfico: %1")
                     .arg(shaderErrors.join(QStringLiteral(", ")));
    }
    localReport.preparedFeatures = cache->preparedFeatures();

    QVector<AssetTask> allTasks = collectAssetTasks(runtime, options);
    QVector<AssetTask> audioTasks;
    qint64 totalWork = 0;
    for (const AssetTask& task : allTasks) {
        if (task.type != QLatin1String("audio")) continue;
        audioTasks.push_back(task);
        totalWork += qMax<qint64>(1, task.workUnits);
    }
    localReport.referencedAssets = audioTasks.size();
    if (audioTasks.isEmpty()) {
        localReport.elapsedMs = elapsed.elapsed();
        if (report) *report = localReport;
        return emitProgress(progress, QStringLiteral("Áudio pronto"), QString(), 0, 0, 0, 0);
    }

    qint64 completed = 0;
    for (int i = 0; i < audioTasks.size(); ++i) {
        const AssetTask& task = audioTasks.at(i);
        if (cache->isWarmedFile(task.absolutePath) &&
            (!audio_policy::shouldCacheAudioBytes(task.size, cache->audioDataBytes()) ||
             cache->containsAudioData(runtime.projectRoot(), task.path))) {
            completed += qMax<qint64>(1, task.workUnits);
            ++localReport.audioFilesWarmed;
            if (cache->containsAudioData(runtime.projectRoot(), task.path)) {
                ++localReport.audioFilesCached;
                localReport.audioBytesCached += cache->audioData(runtime.projectRoot(), task.path).size();
            }
            if (!emitProgress(progress, QStringLiteral("Áudio já preparado"), task.path,
                              completed, totalWork, i + 1, audioTasks.size())) return false;
            continue;
        }
        QString warning;
        if (!preloadAudioFile(runtime, task, cache, &completed, totalWork, i + 1,
                              audioTasks.size(), progress, &localReport, &warning))
            return false;
        if (!warning.isEmpty()) localReport.warnings.push_back(warning);
        if (QFileInfo::exists(task.absolutePath))
            localReport.fileBytesWarmed += qMax<qint64>(0, QFileInfo(task.absolutePath).size());
    }

    localReport.elapsedMs = elapsed.elapsed();
    if (report) *report = localReport;
    return emitProgress(progress, QStringLiteral("Áudio pronto"),
                        QStringLiteral("%1 arquivo(s)").arg(localReport.audioFilesWarmed),
                        totalWork, totalWork, audioTasks.size(), audioTasks.size());
}

std::unique_ptr<core::Editor> prepareRuntimeProject(
    const core::Editor& source, const RuntimePreloadOptions& options,
    RuntimePreloadReport* report, QString* error,
    const RuntimePreloadProgressCallback& progress)
{
    if (error) error->clear();
    RuntimePreloadReport localReport;
    QElapsedTimer elapsed;
    elapsed.start();

    if (!emitProgress(progress, QStringLiteral("Preparando o jogo para teste"),
                      source.projectName, 0, 0, 0, 0)) return {};

    QString snapshotError;
    std::unique_ptr<core::Editor> runtime = core::makeRuntimeEditorSnapshot(source, &snapshotError);
    if (!runtime) {
        if (error) *error = snapshotError;
        return {};
    }

    auto cache = std::make_shared<core::RuntimePreloadCache>();
    runtime->setRuntimePreloadCache(cache);

    int requestedMap = -1;
    QVector<AssetTask> tasks = collectAssetTasks(*runtime, options, &requestedMap);

    QVector<int> mapIndexes;
    if (options.scope == RuntimePreloadScope::FullGame) {
        mapIndexes.reserve(runtime->docs.size());
        for (int i = 0; i < runtime->docs.size(); ++i) mapIndexes.push_back(i);
    } else if (requestedMap >= 0) {
        mapIndexes.push_back(requestedMap);
    }

    cache->setProjectDigest(runtimeProjectDigest(*runtime));
    planRuntimeFeatures(*runtime, cache);
    localReport.requestedFeatures = cache->requestedFeatures();
    localReport.referencedAssets = tasks.size();

    registerEmbeddedImages(*runtime, cache, mapIndexes);

    // O peso é derivado do trabalho que de fato será feito. Imagens que já
    // existem embutidas no Runtime Snapshot usam só uma unidade pequena de
    // indexação/COW; arquivos externos usam seus bytes reais de I/O/decode.
    for (AssetTask& task : tasks) {
        const bool embeddedImage = task.type == QLatin1String("image") &&
                                   cache->containsImage(runtime->projectRoot(), task.path);
        task.workUnits = embeddedImage ? 4096 : qMax<qint64>(1, task.size);
    }

    // Mapas usam células como unidade de cálculo derivado. A barra não avança
    // por timer, duração estimada ou animação cosmética.
    qint64 totalWork = 1; // finalização/paridade
    for (const AssetTask& task : tasks) totalWork += task.workUnits;
    QVector<qint64> mapWeights;
    mapWeights.reserve(mapIndexes.size());
    for (int index : mapIndexes) {
        const core::MapInfo& info = runtime->docs.at(index).map;
        const qint64 weight = qMax<qint64>(4096, qint64(qMax(0, info.width)) * qMax(0, info.height) * 16);
        mapWeights.push_back(weight);
        totalWork += weight;
    }
    totalWork += 64 * 1024; // desserialização dos QShader necessários
    if (cache->requestsFeature(core::RuntimePreloadFeatures::filterComposedSampling()))
        totalWork += 4096; // preparação do contrato de composição pesada

    qint64 completed = 0;
    const int featureItems = cache->requestsFeature(core::RuntimePreloadFeatures::filterComposedSampling()) ? 1 : 0;
    const int totalItems = tasks.size() + mapIndexes.size() + 2 + featureItems;
    int item = 0;
    if (!emitProgress(progress, QStringLiteral("Organizando recursos"),
                      QStringLiteral("Arquivos: %1 · Mapas: %2").arg(tasks.size()).arg(mapIndexes.size()),
                      completed, totalWork, item, totalItems)) return {};

    // Assets externos: imagens são decodificadas e guardadas no Runtime Cache;
    // áudio/outros são lidos para aquecer filesystem/page cache. Fontes usam o
    // mesmo registry compartilhado que GameSession, evitando trabalho duplicado.
    for (const AssetTask& task : tasks) {
        ++item;
        QString warning;
        bool touchedFile = false;
        if (task.type == QLatin1String("image")) {
            const bool alreadyPrepared = cache->containsImage(runtime->projectRoot(), task.path);
            if (!alreadyPrepared) {
                QImage image;
                image.load(task.absolutePath);
                touchedFile = QFileInfo::exists(task.absolutePath);
                if (!image.isNull()) cache->insertImage(runtime->projectRoot(), task.path, image);
                else warning = QStringLiteral("Imagem referenciada não pôde ser decodificada: %1").arg(task.path);
            }
            completed += task.workUnits;
            if (touchedFile) cache->markWarmedFile(task.absolutePath);
            if (cache->containsImage(runtime->projectRoot(), task.path)) ++localReport.imagesPrepared;
            if (!emitProgress(progress, QStringLiteral("Carregando imagens"), task.path,
                              completed, totalWork, item, totalItems)) return {};
        } else if (task.type == QLatin1String("font")) {
            const bool loaded = carregarFonteDoProjeto(task.absolutePath);
            if (!loaded) warning = QStringLiteral("Fonte referenciada não pôde ser carregada: %1").arg(task.path);
            touchedFile = QFileInfo::exists(task.absolutePath);
            if (loaded && touchedFile) cache->markWarmedFile(task.absolutePath);
            completed += task.workUnits;
            if (loaded) ++localReport.fontsPrepared;
            if (!emitProgress(progress, QStringLiteral("Preparando fontes"), task.path,
                              completed, totalWork, item, totalItems)) return {};
        } else if (task.type == QLatin1String("audio")) {
            if (!preloadAudioFile(*runtime, task, cache, &completed, totalWork,
                                  item, totalItems, progress, &localReport, &warning))
                return {};
            touchedFile = QFileInfo::exists(task.absolutePath);
        } else {
            if (!warmFileBytes(task, cache, &completed, totalWork, item, totalItems, progress, &warning))
                return {};
            touchedFile = QFileInfo::exists(task.absolutePath);
            if (touchedFile) ++localReport.otherFilesWarmed;
        }
        if (!warning.isEmpty()) localReport.warnings.push_back(warning);
        if (touchedFile)
            localReport.fileBytesWarmed += qMax<qint64>(0, QFileInfo(task.absolutePath).size());
    }

    // Cache derivado de mapas: World faz o mesmo cálculo canônico de colisão e
    // índice espacial usado no runtime; depois o resultado é transferido para
    // o World definitivo em vez de ser descartado.
    const int originalMap = runtime->activeDocIdx;
    for (int m = 0; m < mapIndexes.size(); ++m) {
        ++item;
        const int index = mapIndexes.at(m);
        runtime->switchDoc(index);
        const QString mapName = runtime->docs.at(index).name;
        if (!emitProgress(progress, QStringLiteral("Preparando mapas"), mapName,
                          completed, totalWork, item, totalItems)) return {};
        World warmWorld(*runtime);
        cache->insertMapCache(runtime->docs.at(index).id, warmWorld.derivedCacheSnapshot());
        completed += mapWeights.at(m);
        ++localReport.mapsPrepared;
        if (!emitProgress(progress, QStringLiteral("Preparando mapas"), mapName,
                          completed, totalWork, item, totalItems)) return {};
    }
    if (originalMap >= 0 && originalMap < runtime->docs.size()) runtime->switchDoc(originalMap);

    ++item;
    QStringList shaderErrors;
    localReport.shadersWarmed = warmRuntimeShaders(&shaderErrors);
    cache->setShadersWarmed(localReport.shadersWarmed);
    completed += 64 * 1024;
    if (!localReport.shadersWarmed) {
        if (error) *error = QStringLiteral("Não foi possível preparar o renderizador gráfico: %1").arg(shaderErrors.join(QStringLiteral(", ")));
        return {};
    }
    cache->markFeaturePrepared(QStringLiteral("runtime-shaders"));
    if (cache->requestsFeature(core::RuntimePreloadFeatures::filterSystem()))
        cache->markFeaturePrepared(core::RuntimePreloadFeatures::filterSystem());
    if (!emitProgress(progress, QStringLiteral("Preparando gráficos"),
                      QStringLiteral("sprite.vert.qsb + sprite.frag.qsb + filter.frag.qsb"),
                      completed, totalWork, item, totalItems)) return {};

    if (cache->requestsFeature(core::RuntimePreloadFeatures::filterComposedSampling())) {
        ++item;
        // QRhiGraphicsPipeline depende do dispositivo real e só pode ser criado
        // em RhiGameWindow::initialize(). Aqui preparamos e registramos o plano
        // para que a janela faça o warm-up imediatamente ao receber o QRhi,
        // antes do primeiro frame jogável, sem descoberta tardia no gameplay.
        cache->markFeaturePrepared(core::RuntimePreloadFeatures::filterComposedSampling());
        completed += 4096;
        if (!emitProgress(progress, QStringLiteral("Preparando efeitos visuais"),
                          QStringLiteral("Blur e Tilt-Shift"),
                          completed, totalWork, item, totalItems)) return {};
    }
    localReport.preparedFeatures = cache->preparedFeatures();

    // O preload só pode adicionar caches derivados/transientes. Qualquer
    // alteração no payload executável é regressão e aborta F5/F6.
    const QStringList parity = core::runtimeProjectParityIssues(source, *runtime);
    if (!parity.isEmpty()) {
        if (error) *error = QStringLiteral("A preparação do jogo alterou dados que deveriam permanecer iguais: %1")
                                .arg(parity.join(QStringLiteral(", ")));
        return {};
    }

    ++item;
    completed = totalWork;
    if (!emitProgress(progress, QStringLiteral("Pronto"),
                      QStringLiteral("Jogo pronto para iniciar"), completed, totalWork,
                      item, totalItems)) return {};

    localReport.elapsedMs = elapsed.elapsed();
    if (report) *report = localReport;
    return runtime;
}

} // namespace game
