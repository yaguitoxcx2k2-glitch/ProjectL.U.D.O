// ============================================================================
// ResourceManager.h — inventário/event-bus de assets do LUDO Map Editor.
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

namespace core {

class ResourceManager : public QObject
{
    Q_OBJECT
public:
    explicit ResourceManager(QObject* parent = nullptr);

    /// Arquivos existentes abaixo de Assets/, em caminhos relativos ao projeto.
    QStringList files(const QString& projectRoot,
                      const QString& relativeFolder = QStringLiteral("Assets")) const;
    QStringList imageFiles(const QString& projectRoot,
                           const QString& relativeFolder = QStringLiteral("Assets")) const;

    /// Invalida somente caches derivados; nunca altera projeto/AssetDatabase.
    void invalidateCaches();
    void notifyAssetsChanged(const QStringList& projectRelativePaths = QStringList());

signals:
    void assetsChanged(const QStringList& projectRelativePaths);
    void imagesChanged();

private:
    QString cacheKey(const QString& projectRoot, const QString& relativeFolder) const;
    mutable QHash<QString, QStringList> m_filesCache;
    mutable QHash<QString, QStringList> m_imagesCache;
};

} // namespace core
