#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <functional>
namespace game {
enum class RuntimeStreamPriority { Background=0, Nearby=1, Immediate=2 };
enum class RuntimeStreamState { Queued, Loading, ReadyForUpload, Complete, Failed, Cancelled };
struct RuntimeStreamRequest { QString key; QString sourcePath; RuntimeStreamPriority priority=RuntimeStreamPriority::Background; qint64 estimatedBytes=0; quint64 generation=0; };
struct RuntimeStreamItem { RuntimeStreamRequest request; RuntimeStreamState state=RuntimeStreamState::Queued; QByteArray payload; QString error; };
class RuntimeStreamingQueue {
public:
    bool enqueue(const RuntimeStreamRequest& request);
    bool cancel(const QString& key);
    bool beginNext(RuntimeStreamItem* item=nullptr);
    bool completeCpuLoad(const QString& key,const QByteArray& payload,QString error={});
    bool markUploaded(const QString& key);
    QVector<RuntimeStreamItem> readyForUpload(qint64 byteBudget) const;
    RuntimeStreamItem item(const QString& key) const;
    int pendingCount() const;
    void clearGeneration(quint64 generation);
private:
    int bestQueuedIndex() const;
    QVector<RuntimeStreamItem> m_items;
};
} // namespace game
