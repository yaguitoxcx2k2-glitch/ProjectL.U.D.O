#include "RuntimePreloadCache.h"

#include <QDir>
#include <QFileInfo>

namespace core {

QString RuntimePreloadCache::imageKey(const QString& projectRoot, const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    QFileInfo info(trimmed);
    const QString absolute = info.isAbsolute() ? info.absoluteFilePath()
        : QDir(projectRoot).absoluteFilePath(trimmed);
    return QDir::cleanPath(QFileInfo(absolute).absoluteFilePath()).toLower();
}

void RuntimePreloadCache::insertImage(const QString& projectRoot, const QString& path,
                                      const QImage& image)
{
    if (image.isNull()) return;
    const QString key = imageKey(projectRoot, path);
    if (!key.isEmpty()) m_images.insert(key, image);
}

QImage RuntimePreloadCache::image(const QString& projectRoot, const QString& path) const
{
    const QString key = imageKey(projectRoot, path);
    const auto it = m_images.constFind(key);
    return it == m_images.cend() ? QImage() : it.value();
}

bool RuntimePreloadCache::containsImage(const QString& projectRoot, const QString& path) const
{
    const QString key = imageKey(projectRoot, path);
    return !key.isEmpty() && m_images.contains(key);
}


void RuntimePreloadCache::insertAudioData(const QString& projectRoot, const QString& path,
                                          const QByteArray& bytes)
{
    if (bytes.isEmpty()) return;
    const QString key = imageKey(projectRoot, path);
    if (!key.isEmpty()) m_audioData.insert(key, bytes);
}

QByteArray RuntimePreloadCache::audioData(const QString& projectRoot, const QString& path) const
{
    const QString key = imageKey(projectRoot, path);
    const auto it = m_audioData.constFind(key);
    return it == m_audioData.cend() ? QByteArray() : it.value();
}

bool RuntimePreloadCache::containsAudioData(const QString& projectRoot, const QString& path) const
{
    const QString key = imageKey(projectRoot, path);
    return !key.isEmpty() && m_audioData.contains(key);
}

qint64 RuntimePreloadCache::audioDataBytes() const
{
    qint64 total = 0;
    for (auto it = m_audioData.cbegin(); it != m_audioData.cend(); ++it)
        total += it.value().size();
    return total;
}

void RuntimePreloadCache::markWarmedFile(const QString& absolutePath)
{
    if (absolutePath.trimmed().isEmpty()) return;
    m_warmedFiles.insert(QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath()).toLower());
}

bool RuntimePreloadCache::isWarmedFile(const QString& absolutePath) const
{
    return m_warmedFiles.contains(QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath()).toLower());
}

QStringList RuntimePreloadCache::warmedFiles() const
{
    QStringList out = m_warmedFiles.values();
    out.sort(Qt::CaseInsensitive);
    return out;
}

void RuntimePreloadCache::insertMapCache(const QString& mapId, const RuntimeMapDerivedCache& cache)
{
    if (!mapId.trimmed().isEmpty()) m_maps.insert(mapId, cache);
}

const RuntimeMapDerivedCache* RuntimePreloadCache::mapCache(const QString& mapId) const
{
    const auto it = m_maps.constFind(mapId);
    return it == m_maps.cend() ? nullptr : &it.value();
}

QStringList RuntimePreloadCache::preparedMapIds() const
{
    QStringList out = m_maps.keys();
    out.sort(Qt::CaseInsensitive);
    return out;
}

void RuntimePreloadCache::requestFeature(const QString& featureId)
{
    const QString id = featureId.trimmed();
    if (!id.isEmpty()) m_requestedFeatures.insert(id);
}

bool RuntimePreloadCache::requestsFeature(const QString& featureId) const
{
    return m_requestedFeatures.contains(featureId.trimmed());
}

QStringList RuntimePreloadCache::requestedFeatures() const
{
    QStringList out = m_requestedFeatures.values();
    out.sort(Qt::CaseInsensitive);
    return out;
}

void RuntimePreloadCache::markFeaturePrepared(const QString& featureId)
{
    const QString id = featureId.trimmed();
    if (!id.isEmpty()) m_preparedFeatures.insert(id);
}

bool RuntimePreloadCache::featurePrepared(const QString& featureId) const
{
    return m_preparedFeatures.contains(featureId.trimmed());
}

QStringList RuntimePreloadCache::preparedFeatures() const
{
    QStringList out = m_preparedFeatures.values();
    out.sort(Qt::CaseInsensitive);
    return out;
}

} // namespace core
