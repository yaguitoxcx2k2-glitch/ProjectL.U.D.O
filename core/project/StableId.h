#pragma once

#include <QString>
#include <QUuid>
#include <QMetaType>
#include <utility>

namespace core {

template <typename Tag>
class StableId final
{
public:
    StableId() = default;
    explicit StableId(QString value) : m_value(std::move(value)) {}

    static StableId create()
    {
        return StableId(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    static StableId fromLegacy(const QString& value)
    {
        return StableId(value.trimmed());
    }

    bool isNull() const { return m_value.isEmpty(); }
    bool isValid() const { return !m_value.isEmpty(); }
    const QString& toString() const { return m_value; }

    friend bool operator==(const StableId& a, const StableId& b) { return a.m_value == b.m_value; }
    friend bool operator!=(const StableId& a, const StableId& b) { return !(a == b); }
    friend bool operator<(const StableId& a, const StableId& b) { return a.m_value < b.m_value; }

private:
    QString m_value;
};

struct MapIdTag;
struct EventIdTag;
struct ActorIdTag;
struct ItemIdTag;
struct SkillIdTag;
struct AssetIdTag;
struct CommonEventIdTag;
struct UiScreenIdTag;

using MapId = StableId<MapIdTag>;
using EventId = StableId<EventIdTag>;
using ActorId = StableId<ActorIdTag>;
using ItemId = StableId<ItemIdTag>;
using SkillId = StableId<SkillIdTag>;
using AssetId = StableId<AssetIdTag>;
using CommonEventId = StableId<CommonEventIdTag>;
using UiScreenId = StableId<UiScreenIdTag>;

template <typename Tag>
uint qHash(const StableId<Tag>& id, uint seed = 0)
{
    return ::qHash(id.toString(), seed);
}

} // namespace core
