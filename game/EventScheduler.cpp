#include "EventScheduler.h"
#include <algorithm>
namespace game {
EventScheduleTicket EventScheduler::reserve(EventLane lane, QString source, int priority) {
    EventScheduleTicket ticket{m_nextSerial++, lane, std::move(source), priority};
    m_pending.push_back(ticket); return ticket;
}
void EventScheduler::complete(quint64 serial) {
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [serial](const auto& v){return v.serial==serial;}), m_pending.end());
}
QVector<EventScheduleTicket> EventScheduler::pending() const { return m_pending; }
void EventScheduler::clear() { m_pending.clear(); }
}
