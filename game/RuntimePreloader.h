// ============================================================================
// RuntimePreloader.h — pipeline canônica de preparação de F5/F6 (RC2.53).
//
// O Editor cria primeiro o RuntimeProjectSnapshot validado e só então prepara
// os recursos executáveis. RC2.63 também reutiliza o estágio de áudio no Player
// exportado; o preload continua sendo otimização e nunca requisito de execução.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace core { class Editor; class RuntimePreloadCache; }

namespace game {

enum class RuntimePreloadScope { CurrentMap, FullGame };

struct RuntimePreloadOptions {
    RuntimePreloadScope scope = RuntimePreloadScope::FullGame;
    QString mapId;
};

struct RuntimePreloadProgress {
    QString stage;
    QString detail;
    qint64 completedWork = 0;
    qint64 totalWork = 0; // 0 = fase indeterminada, nunca progresso falso
    int completedItems = 0;
    int totalItems = 0;
};

using RuntimePreloadProgressCallback = std::function<bool(const RuntimePreloadProgress&)>;

/// Digest do payload executável canônico. Serve para impedir que caches
/// preparados para uma geração antiga sejam anexados a um snapshot novo.
QByteArray runtimeProjectDigest(const core::Editor& editor);
bool runtimePreloadCacheMatches(
    const core::Editor& editor,
    const std::shared_ptr<core::RuntimePreloadCache>& cache);

struct RuntimePreloadReport {
    int mapsPrepared = 0;
    int referencedAssets = 0;
    int imagesPrepared = 0;
    int audioFilesWarmed = 0;
    int audioFilesCached = 0;
    qint64 audioBytesCached = 0;
    int fontsPrepared = 0;
    int otherFilesWarmed = 0;
    qint64 fileBytesWarmed = 0;
    qint64 elapsedMs = 0;
    bool shadersWarmed = false;
    QStringList requestedFeatures;
    QStringList preparedFeatures;
    QStringList warnings;
};


/// Prepara somente áudio no Editor/runtime já carregado. Reutiliza exatamente
/// o mesmo catálogo de referências, orçamento e cache do F5/F6. É usado pelo
/// Player exportado antes de liberar gameplay para reduzir hitch no primeiro play.
bool prepareRuntimeAudioAssets(
    core::Editor& runtime,
    const RuntimePreloadOptions& options = {},
    RuntimePreloadReport* report = nullptr,
    QString* error = nullptr,
    const RuntimePreloadProgressCallback& progress = {});

/// Cria o snapshot executável e o prepara. Retorna nullptr em erro/cancelamento.
/// `error` recebe mensagem vazia em cancelamento voluntário.
std::unique_ptr<core::Editor> prepareRuntimeProject(
    const core::Editor& source,
    const RuntimePreloadOptions& options,
    RuntimePreloadReport* report = nullptr,
    QString* error = nullptr,
    const RuntimePreloadProgressCallback& progress = {});

} // namespace game
