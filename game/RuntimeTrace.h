#pragma once
#include <QJsonArray>
#include <QElapsedTimer>
#include <QString>
namespace game {
class RuntimeTraceRecorder {
public:
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }
    void begin(QString name);
    void end(QString name);
    void clear(){m_events=QJsonArray();}
    QJsonArray events() const { return m_events; }
    bool writeChromeTrace(const QString& path, QString* error=nullptr) const;
private:
    bool m_enabled = false;
    QElapsedTimer m_clock;
    QJsonArray m_events;
};
class RuntimeTraceScope {
public:
    RuntimeTraceScope(RuntimeTraceRecorder* r, QString n):m_r(r),m_n(std::move(n)){if(m_r)m_r->begin(m_n);} ~RuntimeTraceScope(){if(m_r)m_r->end(m_n);}
private: RuntimeTraceRecorder* m_r; QString m_n;
};
}
