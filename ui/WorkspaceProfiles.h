#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ui {

struct WorkspaceProfileDescriptor
{
    QString id;
    QString name;
    QString description;
    bool builtIn = false;
};

/// Perfis locais de layout do Editor. Guardam somente o QByteArray retornado
/// por QMainWindow::saveState(); nunca entram no arquivo .ludo.
class WorkspaceProfiles
{
public:
    static QVector<WorkspaceProfileDescriptor> catalog();
    static QString currentProfileId();
    static void setCurrentProfileId(const QString& id);

    static QByteArray lastSessionState();
    static void setLastSessionState(const QByteArray& state);

    static QByteArray customProfileState(const QString& id);
    static QString saveCustomProfile(const QString& name, const QByteArray& state,
                                     QString* error = nullptr);
    static bool removeCustomProfile(const QString& id, QString* error = nullptr);

    static bool isBuiltIn(const QString& id);
};

} // namespace ui
