#pragma once
#include <QString>
#include <QVector>

namespace game {
enum class EventLane { Main, Parallel, Autorun, Common, Sensor, System };
struct EventScheduleTicket { quint64 serial=0; EventLane lane=EventLane::Main; QString source; int priority=0; };

/// Canonical ordering policy for event work. Existing GameSession paths can
/// migrate lane-by-lane without changing event semantics.
class EventScheduler {
public:
    EventScheduleTicket reserve(EventLane lane, QString source, int priority = 0);
    void complete(quint64 serial);
    QVector<EventScheduleTicket> pending() const;
    void clear();
private:
    quint64 m_nextSerial = 1;
    QVector<EventScheduleTicket> m_pending;
};
}
