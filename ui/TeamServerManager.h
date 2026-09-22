#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QWidget;

namespace ui {
class CollaborationClient;

// Owns the local Team Server process started from the editor. The Python server
// remains the single source of truth; this class only provides lifecycle/UI.
class TeamServerManager : public QObject {
public:
    explicit TeamServerManager(CollaborationClient* collaboration, QWidget* parent);

    void showTeamHub();
    void showHostDialog();
    bool isRunning();
    bool prepareForEditorClose();

private:
    struct PythonCommand {
        QString program;
        QStringList prefix;
        bool valid() const { return !program.isEmpty(); }
    };

    PythonCommand pythonCommand() const;
    QString serverScriptPath() const;
    QString defaultDataFolder() const;
    QString tailscaleAddress() const;
    QString localAddress() const;
    QString sharedAddress() const;
    QString processErrorText() const;
    QString stopSignalPath() const;
    bool localServerResponding() const;
    int localServerTeamProtocol() const;
    bool requestGracefulStop();

    bool runServerTool(const QStringList& arguments, QByteArray* output = nullptr,
                       const QByteArray& stdinData = QByteArray());
    bool hasAdministrator();
    void managePeople();
    bool addPersonDialog();
    bool removePersonDialog();
    bool startServer();
    void stopServer();
    bool keepServerRunningDetached();
    void rememberSettings();

    CollaborationClient* collaboration = nullptr;
    QWidget* window = nullptr;
    QProcess process;
    QString lastOutput;
    QString lastProgram;
    QStringList lastArguments;
    QString lastWorkingDirectory;
    QString dataFolder;
    QString serverName;
    int port = 8787;
    bool allowNetwork = true;
    bool detachedByEditor = false;
};
}
