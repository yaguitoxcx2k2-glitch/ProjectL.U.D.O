// ============================================================================
// RuntimePreloadCache.h — caches transientes preparados pelo Editor antes de
// F5/F6 e pelo Player antes do gameplay. Nunca são persistidos no .ludo, no
// SaveFormat ou no Runtime Snapshot.
//
// RC2.53 / Bloco B: o preload deixa de ser uma barra cosmética. Imagens
// externas já decodificadas e caches derivados de mapas atravessam a fronteira
// Editor -> Runtime e são realmente reutilizados pelo Player/World.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

namespace RuntimePreloadFeatures {
inline QString filterSystem() { return QStringLiteral("filter-system"); }
inline QString filterComposedSampling() { return QStringLiteral("filter-composed-sampling"); }
inline QString filterGpuPipelines() { return QStringLiteral("filter-gpu-pipelines"); }
inline QString command(const QString& type) { return QStringLiteral("command:") + type.trimmed(); }
}

struct RuntimeMapDerivedCache {
    QSize collisionSize;
    QVector<quint8> collisionGrid;
    QHash<int, QVector<QString>> eventSpatialIndex;
    QHash<QString, QPoint> eventIndexedCells;
    QHash<QString, int> eventDefinitionIndex;

    bool hasCollisionGrid() const
    {
        return collisionSize.width() >= 0 && collisionSize.height() >= 0 &&
               collisionGrid.size() == collisionSize.width() * collisionSize.height();
    }
};

class RuntimePreloadCache
{
public:
    static QString imageKey(const QString& projectRoot, const QString& path);

    void insertImage(const QString& projectRoot, const QString& path, const QImage& image);
    QImage image(const QString& projectRoot, const QString& path) const;
    bool containsImage(const QString& projectRoot, const QString& path) const;

    /// Bytes de áudio pequenos/médios podem ser mantidos no cache transiente.
    /// Arquivos acima do orçamento continuam apenas aquecidos no filesystem.
    void insertAudioData(const QString& projectRoot, const QString& path, const QByteArray& bytes);
    QByteArray audioData(const QString& projectRoot, const QString& path) const;
    bool containsAudioData(const QString& projectRoot, const QString& path) const;
    qint64 audioDataBytes() const;
    int audioDataFiles() const { return m_audioData.size(); }

    void markWarmedFile(const QString& absolutePath);
    bool isWarmedFile(const QString& absolutePath) const;
    QStringList warmedFiles() const;

    void insertMapCache(const QString& mapId, const RuntimeMapDerivedCache& cache);
    const RuntimeMapDerivedCache* mapCache(const QString& mapId) const;
    QStringList preparedMapIds() const;

    void setShadersWarmed(bool warmed) { m_shadersWarmed = warmed; }
    bool shadersWarmed() const { return m_shadersWarmed; }

    // Features transientes detectadas pelo RuntimePreloader. A lista nunca é
    // persistida no projeto; consumidores de Runtime podem usá-la para evitar
    // preparar subsistemas/pipelines que o snapshot não referencia.
    void requestFeature(const QString& featureId);
    bool requestsFeature(const QString& featureId) const;
    QStringList requestedFeatures() const;
    void markFeaturePrepared(const QString& featureId);
    bool featurePrepared(const QString& featureId) const;
    QStringList preparedFeatures() const;
    void setProjectDigest(const QByteArray& digest) { m_projectDigest = digest; }
    QByteArray projectDigest() const { return m_projectDigest; }

private:
    QHash<QString, QImage> m_images;
    QHash<QString, QByteArray> m_audioData;
    QSet<QString> m_warmedFiles;
    QHash<QString, RuntimeMapDerivedCache> m_maps;
    bool m_shadersWarmed = false;
    QSet<QString> m_requestedFeatures;
    QSet<QString> m_preparedFeatures;
    QByteArray m_projectDigest;
};

} // namespace core
