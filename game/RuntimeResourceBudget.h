#pragma once
#include <QtGlobal>
#include <QString>
#include <QVector>
namespace game {
enum class RuntimeResourceClass { Textures, Audio, MapChunks, Pictures, DerivedData };
struct RuntimeResourceBudgetLimits {
    qint64 textures = 256ll*1024*1024;
    qint64 audio = 128ll*1024*1024;
    qint64 mapChunks = 64ll*1024*1024;
    qint64 pictures = 128ll*1024*1024;
    qint64 derivedData = 256ll*1024*1024;
};
struct RuntimeResourceUsage {
    RuntimeResourceClass resourceClass = RuntimeResourceClass::Textures;
    qint64 usedBytes = 0;
    qint64 budgetBytes = 0;
    qint64 peakBytes = 0;
    int evictions = 0;
    bool overBudget() const { return budgetBytes > 0 && usedBytes > budgetBytes; }
};
class RuntimeResourceBudget {
public:
    explicit RuntimeResourceBudget(RuntimeResourceBudgetLimits limits = {});
    void setLimits(RuntimeResourceBudgetLimits limits);
    RuntimeResourceBudgetLimits limits() const { return m_limits; }
    void setUsage(RuntimeResourceClass resourceClass, qint64 bytes);
    void addUsage(RuntimeResourceClass resourceClass, qint64 deltaBytes);
    void recordEviction(RuntimeResourceClass resourceClass, qint64 releasedBytes);
    RuntimeResourceUsage usage(RuntimeResourceClass resourceClass) const;
    QVector<RuntimeResourceUsage> snapshot() const;
    qint64 bytesOverBudget(RuntimeResourceClass resourceClass) const;
    static QString resourceClassId(RuntimeResourceClass resourceClass);
private:
    RuntimeResourceUsage& mutableUsage(RuntimeResourceClass resourceClass);
    qint64 limitFor(RuntimeResourceClass resourceClass) const;
    RuntimeResourceBudgetLimits m_limits;
    QVector<RuntimeResourceUsage> m_usage;
};
} // namespace game
