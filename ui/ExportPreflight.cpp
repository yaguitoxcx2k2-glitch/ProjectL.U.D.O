#include "ExportPreflight.h"

#include "core/Editor.h"
#include "core/ProjectIO.h"
#include "core/ResourceManager.h"
#include "core/ProjectDependencyIndex.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <algorithm>

namespace ui {
namespace {
bool isAudioAsset(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("ogg") || suffix == QLatin1String("wav") ||
           suffix == QLatin1String("mp3") || suffix == QLatin1String("flac") ||
           suffix == QLatin1String("m4a") || suffix == QLatin1String("aac") ||
           suffix == QLatin1String("opus");
}

void sortUnique(QStringList& values)
{
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
}
}

ExportPreflightResult ExportPreflight::inspect(const core::Editor& editor)
{
    ExportPreflightResult result;
    result.health = core::ProjectHealth::inspect(editor);
    if (result.health.validation.hasErrors())
        result.blockers << QObject::tr("Problemas que impedem a exportação: %1.")
                               .arg(result.health.validation.errorCount);
    else if (result.health.validation.warningCount > 0)
        result.warnings << QObject::tr("Avisos para revisar antes de exportar: %1.")
                               .arg(result.health.validation.warningCount);

#ifdef Q_OS_WIN
    result.sourcePlayerName = QStringLiteral("LudoPlayer.exe");
#else
    result.sourcePlayerName = QStringLiteral("LudoPlayer");
#endif
    result.sourcePlayerPath = QDir(QCoreApplication::applicationDirPath()).filePath(result.sourcePlayerName);
    if (!QFileInfo::exists(result.sourcePlayerPath))
        result.blockers << QObject::tr("O %1 não foi encontrado ao lado do LudoEngine.").arg(result.sourcePlayerName);

    for (const QString& shader : {QStringLiteral(":/shaders/sprite.vert.qsb"),
                                  QStringLiteral(":/shaders/sprite.frag.qsb"),
                                  QStringLiteral(":/shaders/filter.frag.qsb")}) {
        QFile file(shader);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0)
            result.blockers << QObject::tr("Shader obrigatório indisponível: %1").arg(shader);
    }

    const core::ProjectDependencySnapshot dependencies = core::ProjectDependencyIndex::build(editor);
    QSet<QString> usedKeys;
    for (const core::ProjectDependencyNode& node : dependencies.nodes) {
        if (node.kind != core::ProjectDependencyNodeKind::Asset) continue;
        if (dependencies.incomingCount(node.key) <= 0) continue;
        if (node.missing || node.assetPath.trimmed().isEmpty()) {
            result.missingUsedAssets << (node.assetPath.isEmpty() ? node.assetId : node.assetPath);
            continue;
        }
        const QString path = QDir::fromNativeSeparators(node.assetPath);
        const QString key = path.toLower();
        if (!usedKeys.contains(key)) {
            usedKeys.insert(key);
            result.usedAssets.push_back(path);
        }
    }

    QSet<QString> allKeys;
    for (const QString& path : editor.resources().files(editor.projectRoot())) {
        if (!path.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) continue;
        const QString normalized = QDir::fromNativeSeparators(path);
        const QString key = normalized.toLower();
        if (!allKeys.contains(key)) {
            allKeys.insert(key);
            result.allAssets.push_back(normalized);
        }
    }

    // O conjunto completo é a união do scan físico com os assets usados e
    // resolvidos pelo Asset Database. Assim a opção “todos os assets” nunca
    // perde um recurso válido apenas porque um scanner secundário divergiu.
    for (const QString& used : result.usedAssets) {
        const QString key = used.toLower();
        if (!allKeys.contains(key)) {
            allKeys.insert(key);
            result.allAssets.push_back(used);
        }
    }
    sortUnique(result.usedAssets);
    sortUnique(result.allAssets);
    sortUnique(result.missingUsedAssets);
    result.audioRequired = std::any_of(result.usedAssets.cbegin(), result.usedAssets.cend(), isAudioAsset);

    if (!result.missingUsedAssets.isEmpty())
        result.blockers << QObject::tr("Arquivos usados pelo jogo que não foram encontrados: %1.").arg(result.missingUsedAssets.size());
#ifndef TES_HAS_AUDIO
    if (result.audioRequired)
        result.blockers << QObject::tr("O projeto usa áudio, mas este build da LUDO foi compilado sem Qt Multimedia.");
#endif
    if (result.unusedAssetCount() > 0)
        result.warnings << QObject::tr("Arquivos do projeto sem uso no jogo: %1. Eles podem ser deixados de fora da exportação.")
                               .arg(result.unusedAssetCount());
    return result;
}

} // namespace ui
