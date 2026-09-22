#include "RuntimeDistribution.h"

#include "ProjectIO.h"
#include "ProjectValidator.h"
#include "SecureAssetPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace core {
namespace {

void setError(QString* error, const QString& value)
{
    if (error) *error = value;
}

} // namespace

bool mountRuntimeDistribution(const QString& projectPath,
                              RuntimeDistributionMount* mount,
                              QString* error)
{
    if (!mount) {
        setError(error, QObject::tr("Destino de montagem runtime inválido."));
        return false;
    }

    mount->sourceProjectPath.clear();
    mount->runtimeProjectPath.clear();
    mount->assetPackagePath.clear();
    mount->secureAssets = false;
    mount->temporaryDir.reset();

    const QFileInfo projectInfo(projectPath);
    const QString sourceProject = projectInfo.absoluteFilePath();
    if (!projectInfo.exists() || !projectInfo.isFile()) {
        setError(error, QObject::tr("Projeto não encontrado: %1").arg(sourceProject));
        return false;
    }

    mount->sourceProjectPath = sourceProject;
    mount->runtimeProjectPath = sourceProject;
    mount->assetPackagePath = QDir(projectInfo.absolutePath()).filePath(QStringLiteral("game.assets"));

    if (!QFileInfo::exists(mount->assetPackagePath)) return true;

    auto temporary = std::make_unique<QTemporaryDir>(
        QDir(QDir::tempPath()).filePath(QStringLiteral("LudoPlayer-XXXXXX")));
    if (!temporary->isValid()) {
        setError(error, QObject::tr("Não foi possível preparar os Assets protegidos do jogo."));
        return false;
    }

    QString packageError;
    if (!secure_assets::extract(mount->assetPackagePath, temporary->path(), &packageError)) {
        setError(error, packageError);
        return false;
    }

    const QString runtimeProject = QDir(temporary->path()).filePath(QStringLiteral("game.ludo"));
    if (!QFile::copy(sourceProject, runtimeProject)) {
        setError(error, QObject::tr("Não foi possível preparar game.ludo para execução protegida."));
        return false;
    }

    mount->secureAssets = true;
    mount->runtimeProjectPath = runtimeProject;
    mount->temporaryDir = std::move(temporary);
    return true;
}

bool loadRuntimeDistribution(Editor& editor,
                             const QString& projectPath,
                             RuntimeDistributionMount* mount,
                             QString* error)
{
    if (!mountRuntimeDistribution(projectPath, mount, error)) return false;
    if (!io::loadProject(editor, mount->runtimeProjectPath, error)) return false;
    return true;
}

RuntimeDistributionSmokeResult smokeRuntimeDistribution(const QString& projectPath)
{
    RuntimeDistributionSmokeResult result;
    RuntimeDistributionMount mount;
    Editor editor;
    QString loadError;
    if (!loadRuntimeDistribution(editor, projectPath, &mount, &loadError)) {
        result.errorCount = 1;
        result.diagnostics.push_back(loadError);
        return result;
    }

    const ProjectValidationResult validation = ProjectValidator::validate(editor);
    result.errorCount = validation.errorCount;
    result.warningCount = validation.warningCount;
    for (const ValidationIssue& issue : validation.issues) {
        if (issue.severity == ValidationSeverity::Info) continue;
        result.diagnostics.push_back(
            QStringLiteral("[%1] %2 — %3")
                .arg(validationSeverityLabel(issue.severity), issue.location, issue.message));
    }

    // O start map é requisito de execução do Player e merece uma mensagem
    // explícita mesmo caso um projeto legado escape de outra validação.
    if (editor.mapIndexById(editor.startMapId) < 0) {
        ++result.errorCount;
        result.diagnostics.push_back(QObject::tr("[Erro] Sistema — mapa inicial não existe no payload distribuído."));
    }

    result.ok = result.errorCount == 0;
    return result;
}

} // namespace core
