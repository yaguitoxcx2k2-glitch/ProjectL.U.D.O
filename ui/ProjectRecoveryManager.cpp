#include "ProjectRecoveryManager.h"

#include "core/Editor.h"
#include "core/ProjectIO.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

namespace ui {

ProjectRecoveryManager::ProjectRecoveryManager(core::Editor& editor, QObject* parent)
    : QObject(parent), m_editor(editor)
{
    auto* timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, [this] { writeNow(); });
    timer->start();
}

QString ProjectRecoveryManager::recoveryPathFor(const QString& projectPath)
{
    return projectPath.isEmpty() ? QString() : projectPath + QStringLiteral(".autosave.ludo");
}

bool ProjectRecoveryManager::hasNewerRecovery(const QString& projectPath)
{
    return statusFor(projectPath).newerThanProject;
}


ProjectRecoveryStatus ProjectRecoveryManager::statusFor(const QString& projectPath)
{
    ProjectRecoveryStatus status;
    if (projectPath.trimmed().isEmpty()) return status;
    status.projectPath = QFileInfo(projectPath).absoluteFilePath();
    status.recoveryPath = recoveryPathFor(status.projectPath);
    const QFileInfo projectInfo(status.projectPath);
    const QFileInfo recoveryInfo(status.recoveryPath);
    status.projectExists = projectInfo.exists();
    status.recoveryExists = recoveryInfo.exists();
    if (status.projectExists) status.projectModified = projectInfo.lastModified();
    if (status.recoveryExists) {
        status.recoveryModified = recoveryInfo.lastModified();
        status.recoveryBytes = recoveryInfo.size();
    }
    status.newerThanProject = status.recoveryExists &&
        (!status.projectExists || status.recoveryModified > status.projectModified);
    return status;
}

bool ProjectRecoveryManager::discardRecovery(const QString& projectPath, QString* error)
{
    if (error) error->clear();
    const QString path = recoveryPathFor(projectPath);
    if (path.isEmpty() || !QFileInfo::exists(path)) return true;
    if (QFile::remove(path)) return true;
    if (error) *error = tr("Não foi possível remover a cópia de recuperação.");
    return false;
}

bool ProjectRecoveryManager::writeNow(QString* error)
{
    if (error) error->clear();
    if (!m_editor.projectDirty || m_editor.projectPath.isEmpty()) return true;

    const QByteArray bytes = QJsonDocument(core::io::buildProjectPayload(m_editor))
                                 .toJson(QJsonDocument::Compact);
    QSaveFile file(recoveryPathFor(m_editor.projectPath));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = tr("Não foi possível abrir o autosave de recuperação.");
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = tr("Não foi possível concluir o autosave de recuperação.");
        return false;
    }
    return true;
}

} // namespace ui
