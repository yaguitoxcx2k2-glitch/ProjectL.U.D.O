#pragma once

#include "Editor.h"

#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

namespace core {

// Montagem usada pelo LudoPlayer e pelos testes de distribuição. Quando
// game.assets existe, os Assets são extraídos para uma pasta temporária e o
// game.ludo é executado desse mesmo root para que projectRoot()/AssetDatabase
// enxerguem exatamente a estrutura que o jogo distribuído usará.
struct RuntimeDistributionMount {
    QString sourceProjectPath;
    QString runtimeProjectPath;
    QString assetPackagePath;
    bool secureAssets = false;
    std::unique_ptr<QTemporaryDir> temporaryDir;

    RuntimeDistributionMount() = default;
    RuntimeDistributionMount(RuntimeDistributionMount&&) noexcept = default;
    RuntimeDistributionMount& operator=(RuntimeDistributionMount&&) noexcept = default;
    RuntimeDistributionMount(const RuntimeDistributionMount&) = delete;
    RuntimeDistributionMount& operator=(const RuntimeDistributionMount&) = delete;
};

struct RuntimeDistributionSmokeResult {
    bool ok = false;
    int errorCount = 0;
    int warningCount = 0;
    QStringList diagnostics;
};

/// Prepara o root runtime de uma distribuição plain ou protegida.
bool mountRuntimeDistribution(const QString& projectPath,
                              RuntimeDistributionMount* mount,
                              QString* error = nullptr);

/// Monta e carrega o projeto com o mesmo caminho usado pelo LudoPlayer.
bool loadRuntimeDistribution(Editor& editor,
                             const QString& projectPath,
                             RuntimeDistributionMount* mount,
                             QString* error = nullptr);

/// Smoke estrutural do produto distribuído: monta game.assets (quando houver),
/// carrega game.ludo e executa o Project Validator sobre o conteúdo efetivo.
RuntimeDistributionSmokeResult smokeRuntimeDistribution(const QString& projectPath);

} // namespace core
