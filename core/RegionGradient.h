#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <utility>
#include <queue>
#include <unordered_map>

namespace core {
// Join only touching regions in the selected ID range; zero means outside.
template<class ReadRegion>
inline std::vector<std::pair<int,int>> mergedRegionArea(
    std::vector<std::pair<int,int>> cells, int start, int end, ReadRegion read)
{
    using Key = std::uint64_t;
    auto key=[](int x,int y){return (Key(std::uint32_t(x))<<32)|std::uint32_t(y);};
    std::unordered_map<Key,bool> seen;
    std::vector<std::pair<int,int>> result;
    for (const auto& cell:cells) if(seen.emplace(key(cell.first,cell.second),true).second) result.push_back(cell);
    const int lo=std::min(start,end), hi=std::max(start,end);
    const int dx[]={1,-1,0,0},dy[]={0,0,1,-1};
    for(std::size_t i=0;i<result.size();++i) {
        const auto [x,y]=result[i];
        for(int n=0;n<4;++n) {
            const int nx=x+dx[n],ny=y+dy[n];
            if(!seen.emplace(key(nx,ny),true).second) continue;
            const int value=read(nx,ny);
            if(value>0 && value>=lo && value<=hi) result.emplace_back(nx,ny);
        }
    }
    return result;
}

// Distances from the actual area's boundary. Missing cells (including holes)
// are outside; no geometry is added or removed. Results follow input order.
inline std::vector<int> regionAreaGradient(const std::vector<std::pair<int,int>>& cells, int start, int end)
{
    start = std::clamp(start, 1, 255); end = std::clamp(end, 1, 255);
    using Key = std::uint64_t;
    auto key = [](int x, int y) { return (Key(std::uint32_t(x)) << 32) | std::uint32_t(y); };
    std::unordered_map<Key, std::size_t> indices;
    for (std::size_t i = 0; i < cells.size(); ++i) indices.emplace(key(cells[i].first, cells[i].second), i);
    std::vector<int> depth(cells.size(), -1), values;
    std::queue<std::size_t> queue;
    const int dx[] = {1,-1,0,0}, dy[] = {0,0,1,-1};
    for (const auto& entry : indices) {
        const auto i = entry.second;
        const auto [x,y] = cells[i];
        for (int n = 0; n < 4; ++n) if (!indices.count(key(x+dx[n],y+dy[n]))) {
            depth[i] = 0; queue.push(i); break;
        }
    }
    while (!queue.empty()) {
        const auto i = queue.front(); queue.pop();
        const auto [x,y] = cells[i];
        for (int n = 0; n < 4; ++n) {
            const auto found = indices.find(key(x+dx[n],y+dy[n]));
            if (found == indices.end() || depth[found->second] >= 0) continue;
            depth[found->second] = depth[i]+1; queue.push(found->second);
        }
    }
    values.reserve(cells.size());
    for (const auto& cell : cells) {
        const int distance = depth[indices.at(key(cell.first,cell.second))];
        const int step = std::min(std::abs(end-start), distance);
        values.push_back(start + (end >= start ? step : -step));
    }
    return values;
}

// Stroke-relative distance, shared by preview and every region painting tool.
inline int regionGradientValue(int start, int end, int distance, bool pingPong)
{
    start = std::clamp(start, 1, 255);
    end = std::clamp(end, 1, 255);
    const int span = std::abs(end - start);
    int step = std::max(0, distance);
    if (span > 0 && pingPong) {
        step %= 2 * span;
        if (step > span) step = 2 * span - step;
    } else step = std::min(step, span);
    return start + (end >= start ? step : -step);
}
}
