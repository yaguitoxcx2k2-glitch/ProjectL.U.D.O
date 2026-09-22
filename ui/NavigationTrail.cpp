#include "NavigationTrail.h"

namespace ui {

bool NavigationTrail::same(const core::ProjectReferenceLocation& a,
                           const core::ProjectReferenceLocation& b)
{
    return a.ownerType == b.ownerType && a.ownerId == b.ownerId &&
           a.mapId == b.mapId && a.pageIndex == b.pageIndex &&
           a.commandIndex == b.commandIndex && a.detail == b.detail;
}

void NavigationTrail::clear()
{
    m_entries.clear();
    m_index = -1;
}

void NavigationTrail::push(const core::ProjectReferenceLocation& location)
{
    if (location.ownerType.trimmed().isEmpty()) return;
    if (m_index >= 0 && m_index < m_entries.size() && same(m_entries.at(m_index), location)) return;
    while (m_entries.size() > m_index + 1) m_entries.removeLast();
    m_entries.push_back(location);
    while (m_entries.size() > 80) m_entries.removeFirst();
    m_index = m_entries.size() - 1;
}

bool NavigationTrail::canBack() const { return m_index > 0 && m_index < m_entries.size(); }
bool NavigationTrail::canForward() const { return m_index >= 0 && m_index + 1 < m_entries.size(); }

core::ProjectReferenceLocation NavigationTrail::back()
{
    if (!canBack()) return {};
    --m_index;
    return m_entries.at(m_index);
}

core::ProjectReferenceLocation NavigationTrail::forward()
{
    if (!canForward()) return {};
    ++m_index;
    return m_entries.at(m_index);
}

} // namespace ui
