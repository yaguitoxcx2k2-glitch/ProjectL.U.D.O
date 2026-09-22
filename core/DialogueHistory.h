#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace core {

struct DialogueEntry {
    QString text;
    QString speaker;
    QString speakerId;
    QString mapId;
    QString eventId;
    QDateTime timestamp;
    bool read = false;
};

/// Histórico de falas pertencente à partida. O limite evita que saves cresçam
/// indefinidamente, preservando sempre as entradas mais recentes.
class DialogueHistory {
public:
    int addEntry(DialogueEntry entry);
    int unreadCount() const;
    bool markRead(int index);
    void markAllRead();
    const QVector<DialogueEntry>& entries() const { return m_entries; }
    QVector<DialogueEntry> unreadEntries() const;
    void clear() { m_entries.clear(); }
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& object);

private:
    static constexpr int MaximumEntries = 2048;
    QVector<DialogueEntry> m_entries;
};

} // namespace core
