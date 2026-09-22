// ============================================================================
// MapProjectSave.cpp — gravação atômica do documento .ludo Map Only.
// A serialização continua em ProjectIO por compatibilidade de leitura; a
// política de Save/backup agora vive em um módulo isolado.
// ============================================================================
#include "core/ProjectIO.h"
#include "core/AssetWorkflow.h"
#include "core/serialization/AtomicProjectFile.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QObject>
#include <QSaveFile>

namespace core { namespace io {

bool saveProject(Editor& ed, const QString& path, QString* error)
{
    // projectRoot() depende de projectPath. Em Save As precisamos apontar
    // temporariamente para o novo destino antes de reconciliar Assets/.
    const QString previousProjectPath = ed.projectPath;
    ed.projectPath = path;
    QString assetFolderError;
    if (!AssetWorkflow::ensureProjectFolders(ed.projectRoot(), &assetFolderError)) {
        ed.projectPath = previousProjectPath;
        if (error) *error = QObject::tr("Falha ao preparar as pastas de Assets: %1").arg(assetFolderError);
        return false;
    }
    QString assetDatabaseError;
    if (!ed.assetDatabase.synchronize(ed.projectRoot(), &assetDatabaseError)) {
        ed.projectPath = previousProjectPath;
        if (error) *error = QObject::tr("Falha ao atualizar o Asset Database: %1").arg(assetDatabaseError);
        return false;
    }
    const QVector<AssetPathChange> assetPathChanges = ed.assetDatabase.takePathChanges();
    for (const AssetPathChange& change : assetPathChanges)
        rewriteEditorAssetPath(ed, change.oldPath, change.newPath);
    // Mantém a última versão íntegra recuperável. QSaveFile já impede arquivos
    // pela metade; o .bak protege também contra uma edição válida porém ruim.
    const QString backupPath = path + QStringLiteral(".bak");
    if (QFileInfo::exists(path)) {
        QFile previous(path);
        bool validPrevious = false;
        QByteArray previousBytes;
        if (previous.open(QIODevice::ReadOnly)) {
            previousBytes = previous.readAll();
            QJsonParseError parseError{};
            const QJsonDocument previousDocument = QJsonDocument::fromJson(previousBytes, &parseError);
            validPrevious = parseError.error == QJsonParseError::NoError && previousDocument.isObject();
        }
        if (validPrevious) {
            QSaveFile backup(backupPath);
            if (backup.open(QIODevice::WriteOnly) && backup.write(previousBytes) == previousBytes.size())
                backup.commit();
        }
    }
    const QJsonDocument doc(buildProjectPayload(ed));
    const QByteArray payload = doc.toJson(QJsonDocument::Compact);
    if (!serialization::writeAtomically(path, payload, error)) {
        ed.projectPath = previousProjectPath;
        return false;
    }
    ed.projectPath = path;
    // Nenhuma preparação de runtime é executada após salvar. O documento é
    // somente de autoria; o RPG Maker de destino carrega seus próprios recursos.
    ed.markSaved();
    return true;
}


}} // namespace core::io
