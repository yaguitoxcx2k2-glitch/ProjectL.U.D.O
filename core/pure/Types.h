#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ludo::core {

using Text = std::string;
using TextView = std::string_view;
using Bytes = std::vector<std::uint8_t>;

template <typename Tag>
class StrongId {
public:
    StrongId() = default;
    explicit StrongId(Text value) : value_(std::move(value)) {}

    const Text& value() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }
    explicit operator bool() const noexcept { return !empty(); }

    friend bool operator==(const StrongId& a, const StrongId& b) noexcept { return a.value_ == b.value_; }
    friend bool operator!=(const StrongId& a, const StrongId& b) noexcept { return !(a == b); }
    friend bool operator<(const StrongId& a, const StrongId& b) noexcept { return a.value_ < b.value_; }

private:
    Text value_;
};

struct ProjectTag {}; struct MapTag {}; struct EventTag {}; struct CommonEventTag {};
struct AssetTag {}; struct LayerTag {}; struct TilesetTag {}; struct DatabaseTag {};
using ProjectId = StrongId<ProjectTag>;
using MapId = StrongId<MapTag>;
using EventId = StrongId<EventTag>;
using CommonEventId = StrongId<CommonEventTag>;
using AssetId = StrongId<AssetTag>;
using LayerId = StrongId<LayerTag>;
using TilesetId = StrongId<TilesetTag>;
using DatabaseId = StrongId<DatabaseTag>;

struct PointI { int x = 0; int y = 0; };
struct SizeI {
    int width = 0;
    int height = 0;
    bool valid() const noexcept { return width >= 0 && height >= 0; }
};
struct RectI {
    int x = 0; int y = 0; int width = 0; int height = 0;
    bool empty() const noexcept { return width <= 0 || height <= 0; }
    bool contains(PointI p) const noexcept {
        return p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
    }
};
struct ColorRgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

} // namespace ludo::core

namespace std {
template <typename Tag>
struct hash<ludo::core::StrongId<Tag>> {
    size_t operator()(const ludo::core::StrongId<Tag>& id) const noexcept {
        return hash<std::string>{}(id.value());
    }
};
}
