#include "ResourceManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

namespace core {

ResourceManager::ResourceManager(QObject* parent) : QObject(parent) {}

QString ResourceManager::cacheKey(const QString& projectRoot, const QString& relativeFolder) const
{
    const QString root = QDir::cleanPath(QDir(projectRoot).absolutePath());
    const QString folder = QDir::cleanPath(relativeFolder);
    return root + QLatin1Char('\n') + folder;
}

QStringList ResourceManager::files(const QString& projectRoot, const QString& relativeFolder) const
{
    QStringList out;
    if (projectRoot.isEmpty()) return out;

    const QString key = cacheKey(projectRoot, relativeFolder);
    const auto cached = m_filesCache.constFind(key);
    if (cached != m_filesCache.cend()) return cached.value();

    const QDir root(projectRoot);
    const QString folder = root.filePath(relativeFolder);
    if (!QFileInfo(folder).isDir()) {
        m_filesCache.insert(key, out);
        return out;
    }
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absolute = it.next();
        out.push_back(QDir::fromNativeSeparators(root.relativeFilePath(absolute)));
    }
    out.sort(Qt::CaseInsensitive);
    m_filesCache.insert(key, out);
    return out;
}

QStringList ResourceManager::imageFiles(const QString& projectRoot,
                                        const QString& relativeFolder) const
{
    if (projectRoot.isEmpty()) return {};
    const QString key = cacheKey(projectRoot, relativeFolder);
    const auto cached = m_imagesCache.constFind(key);
    if (cached != m_imagesCache.cend()) return cached.value();

    static const QSet<QString> imageExtensions = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("gif")
    };
    QStringList out;
    for (const QString& path : files(projectRoot, relativeFolder)) {
        if (imageExtensions.contains(QFileInfo(path).suffix().toLower())) out.push_back(path);
    }
    m_imagesCache.insert(key, out);
    return out;
}

void ResourceManager::invalidateCaches()
{
    m_filesCache.clear();
    m_imagesCache.clear();
}

void ResourceManager::notifyAssetsChanged(const QStringList& projectRelativePaths)
{
    // Toda mutação feita pela LUDO passa pelo event bus e invalida o inventário
    // antes que qualquer consumidor reaja ao sinal.
    invalidateCaches();
    emit assetsChanged(projectRelativePaths);
    bool image = projectRelativePaths.isEmpty();
    for (const QString& path : projectRelativePaths) {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QLatin1String("png") || ext == QLatin1String("jpg") ||
            ext == QLatin1String("jpeg") || ext == QLatin1String("bmp") ||
            ext == QLatin1String("webp") || ext == QLatin1String("gif")) {
            image = true;
            break;
        }
    }
    if (image) emit imagesChanged();
}

} // namespace core
