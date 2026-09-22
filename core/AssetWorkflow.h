// ============================================================================
// AssetWorkflow.h — pastas/categorias do LUDO Map Editor.
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

struct AssetCategoryInfo {
    QString id;
    QString displayName;
    QString relativeFolder;
    QStringList aliases;
    QString mediaType;
};

class AssetWorkflow
{
public:
    /// Somente categorias usadas na autoria de mapas.
    static QVector<AssetCategoryInfo> categories();
    static QStringList standardProjectFolders();
    static bool ensureProjectFolders(const QString& projectRoot, QString* error = nullptr);
    static QString categoryIdForPath(const QString& path);
    static QString initialFolderForContext(const QString& contextOrFolder);
    static QString projectFolderForContext(const QString& contextOrFolder);
    static bool isInsideAssets(const QString& projectRoot, const QString& absoluteOrRelativePath);
};

} // namespace core
