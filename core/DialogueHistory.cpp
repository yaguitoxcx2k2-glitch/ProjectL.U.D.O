#include "DialogueHistory.h"

#include <QJsonArray>

namespace core {

int DialogueHistory::addEntry(DialogueEntry entry)
{
    entry.text = entry.text.left(32768);
    entry.speaker = entry.speaker.left(512);
    entry.speakerId = entry.speakerId.left(512);
    entry.mapId = entry.mapId.left(512);
    entry.eventId = entry.eventId.left(512);
    if (!entry.timestamp.isValid()) entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.read = false;
    while (m_entries.size() >= MaximumEntries) m_entries.removeFirst();
    m_entries.push_back(std::move(entry));
    return m_entries.size() - 1;
}

int DialogueHistory::unreadCount() const
{
    int count = 0;
    for (const DialogueEntry& entry : m_entries) if (!entry.read) ++count;
    return count;
}

bool DialogueHistory::markRead(int index)
{
    if (index < 0 || index >= m_entries.size()) return false;
    m_entries[index].read = true;
    return true;
}

void DialogueHistory::markAllRead()
{
    for (DialogueEntry& entry : m_entries) entry.read = true;
}

QVector<DialogueEntry> DialogueHistory::unreadEntries() const
{
    QVector<DialogueEntry> result;
    result.reserve(unreadCount());
    for (const DialogueEntry& entry : m_entries) if (!entry.read) result.push_back(entry);
    return result;
}

QJsonObject DialogueHistory::toJson() const
{
    QJsonArray entries;
    for (const DialogueEntry& entry : m_entries) {
        entries.append(QJsonObject{{QStringLiteral("text"), entry.text},
                                   {QStringLiteral("speaker"), entry.speaker},
                                   {QStringLiteral("speakerId"), entry.speakerId},
                                   {QStringLiteral("mapId"), entry.mapId},
                                   {QStringLiteral("eventId"), entry.eventId},
                                   {QStringLiteral("timestamp"), entry.timestamp.toUTC().toString(Qt::ISODateWithMs)},
                                   {QStringLiteral("read"), entry.read}});
    }
    return QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), entries}};
}

bool DialogueHistory::fromJson(const QJsonObject& object)
{
    clear();
    if (object.isEmpty()) return true;
    const QJsonArray array = object.value(QStringLiteral("entries")).toArray();
    const int first = qMax(0, array.size() - MaximumEntries);
    for (int i = first; i < array.size(); ++i) {
        const QJsonObject value = array.at(i).toObject();
        DialogueEntry entry;
        entry.text = value.value(QStringLiteral("text")).toString().left(32768);
        if (entry.text.isEmpty()) continue;
        entry.speaker = value.value(QStringLiteral("speaker")).toString().left(512);
        entry.speakerId = value.value(QStringLiteral("speakerId")).toString().left(512);
        entry.mapId = value.value(QStringLiteral("mapId")).toString().left(512);
        entry.eventId = value.value(QStringLiteral("eventId")).toString().left(512);
        entry.timestamp = QDateTime::fromString(value.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
        if (!entry.timestamp.isValid()) entry.timestamp = QDateTime::currentDateTimeUtc();
        entry.read = value.value(QStringLiteral("read")).toBool(false);
        m_entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace core
