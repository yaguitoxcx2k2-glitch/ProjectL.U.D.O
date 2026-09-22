// ============================================================================
// ProjectRecoveryManager.h — autosave de recuperacao do EDITOR.
//
// Separado do save do jogo. Nunca marca o projeto como salvo; apenas escreve
// uma copia .autosave.ludo que pode ser oferecida apos fechamento inesperado.
// ============================================================================
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

namespace core { class Editor; }

namespace ui {

struct ProjectRecoveryStatus
{
    QString projectPath;
    QString recoveryPath;
    bool projectExists = false;
    bool recoveryExists = false;
    bool newerThanProject = false;
    qint64 recoveryBytes = 0;
    QDateTime projectModified;
    QDateTime recoveryModified;
};

class ProjectRecoveryManager : public QObject
{
    Q_OBJECT
public:
    explicit ProjectRecoveryManager(core::Editor& editor, QObject* parent = nullptr);

    static QString recoveryPathFor(const QString& projectPath);
    static bool hasNewerRecovery(const QString& projectPath);
    static ProjectRecoveryStatus statusFor(const QString& projectPath);
    static bool discardRecovery(const QString& projectPath, QString* error = nullptr);

    bool writeNow(QString* error = nullptr);

private:
    core::Editor& m_editor;
};

} // namespace ui
