#include "ProjectOpenWorkflow.h"

#include "ProjectRecoveryManager.h"
#include "RecoveryDecisionDialog.h"
#include "core/AssetWorkflow.h"
#include "core/Editor.h"
#include "core/ProjectIO.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QObject>

namespace ui {

bool openProjectWorkflow(core::Editor& editor, const QString& projectPath, QWidget* parent,
                         ProjectOpenResult* result, QString* error)
{
    if (error) error->clear();
    if (result) *result = ProjectOpenResult();
    if (projectPath.trimmed().isEmpty()) {
        if (error) *error = QObject::tr("Caminho de projeto vazio.");
        return false;
    }

    const QString requestedPath = QFileInfo(projectPath).absoluteFilePath();

    // Primeiro resolvemos e validamos a fonte em um Editor de staging. ProjectIO
    // pode sincronizar Asset Database antes de encontrar um erro posterior; por
    // isso uma tentativa inválida nunca deve tocar o projeto que está aberto.
    core::Editor staging;
    QString selectedSource = requestedPath;
    QString loadError;
    bool recoveredBackup = false;
    bool restoredAutosave = false;

    if (!core::io::loadProject(staging, requestedPath, &loadError)) {
        const QString backupPath = requestedPath + QStringLiteral(".bak");
        if (!QFileInfo::exists(backupPath)) {
            if (error) *error = loadError;
            return false;
        }

        const auto answer = QMessageBox::question(
            parent, QObject::tr("Erro ao abrir"),
            loadError + QObject::tr("\n\nExiste uma cópia de recuperação. Deseja abri-la?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            if (error) *error = loadError;
            return false;
        }

        QString recoveryError;
        core::Editor backupStaging;
        if (!core::io::loadProject(backupStaging, backupPath, &recoveryError)) {
            if (error) *error = QObject::tr("A cópia de recuperação também falhou: %1").arg(recoveryError);
            return false;
        }
        selectedSource = backupPath;
        recoveredBackup = true;
    }

    const ProjectRecoveryStatus recoveryStatus = ProjectRecoveryManager::statusFor(requestedPath);
    const QString autosavePath = recoveryStatus.recoveryPath;
    if (recoveryStatus.newerThanProject) {
        RecoveryDecisionDialog recoveryDialog(recoveryStatus, parent);
        if (recoveryDialog.exec() != QDialog::Accepted) {
            if (error) *error = QObject::tr("Abertura cancelada durante a escolha de recuperação.");
            return false;
        }
        if (recoveryDialog.choice() == RecoveryChoice::DiscardRecovery) {
            QString discardError;
            if (!ProjectRecoveryManager::discardRecovery(requestedPath, &discardError))
                QMessageBox::warning(parent, QObject::tr("Recuperação"), discardError);
        } else if (recoveryDialog.choice() == RecoveryChoice::RestoreRecovery) {
            QString recoveryError;
            core::Editor autosaveStaging;
            if (core::io::loadProject(autosaveStaging, autosavePath, &recoveryError)) {
                selectedSource = autosavePath;
                restoredAutosave = true;
            } else {
                QMessageBox::warning(parent, QObject::tr("Recuperação falhou"), recoveryError);
            }
        }
    }

    // A fonte escolhida já foi provada isoladamente. Só agora substituímos o
    // Editor ativo. Se o arquivo mudar entre probe e commit, ainda retornamos um
    // erro explícito em vez de declarar uma abertura parcial como sucesso.
    QString commitError;
    if (!core::io::loadProject(editor, selectedSource, &commitError)) {
        if (error) *error = QObject::tr("O projeto foi validado, mas mudou ou deixou de estar disponível durante a abertura: %1")
                                .arg(commitError);
        return false;
    }

    if (recoveredBackup || restoredAutosave) {
        editor.projectPath = requestedPath;
        editor.projectDirty = true;
        emit editor.projectChanged();
    }

    if (restoredAutosave) {
        QMessageBox::information(
            parent, QObject::tr("Autosave restaurado"),
            QObject::tr("A cópia automática foi carregada. Use Salvar para substituir o projeto principal."));
    } else if (recoveredBackup) {
        QMessageBox::information(
            parent, QObject::tr("Projeto recuperado"),
            QObject::tr("A última cópia íntegra foi aberta. Salve o projeto para restaurar o arquivo principal."));
    }

    QString assetError;
    if (!core::AssetWorkflow::ensureProjectFolders(editor.projectRoot(), &assetError)) {
        // O projeto já foi carregado com sucesso. Falha ao preparar pastas de
        // Assets é degradável e não deve transformar uma abertura válida em
        // falso negativo deixando o editor em estado parcialmente carregado.
        QMessageBox::warning(parent, QObject::tr("Assets"),
                             QObject::tr("O projeto foi aberto, mas algumas pastas de Assets não puderam ser preparadas:\n%1")
                                 .arg(assetError));
    }

    if (result) {
        result->projectPath = requestedPath;
        result->recoveredFromBackup = recoveredBackup;
        result->restoredAutosave = restoredAutosave;
    }
    return true;
}

} // namespace ui
