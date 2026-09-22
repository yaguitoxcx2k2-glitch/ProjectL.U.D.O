#pragma once
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <QtGlobal>

namespace game {
struct RuntimeReplayFrame { quint64 frame=0; QString action; bool pressed=false; };
class RuntimeReplay {
public:
    void setSeed(quint64 seed) { m_seed=seed; }
    quint64 seed() const { return m_seed; }
    void record(quint64 frame, QString action, bool pressed);
    const QVector<RuntimeReplayFrame>& frames() const { return m_frames; }
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject&, QString* error=nullptr);
private:
    quint64 m_seed=0;
    QVector<RuntimeReplayFrame> m_frames;
};
}
