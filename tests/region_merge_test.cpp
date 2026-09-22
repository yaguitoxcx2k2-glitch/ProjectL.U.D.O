#include "core/RegionGradient.h"
#include <cassert>
#include <map>
#include <set>
#include <iostream>
using Cell=std::pair<int,int>;
int main() {
    std::map<Cell,int> previous;
    for(int y=0;y<5;++y) for(int x=0;x<3;++x) previous[{x,y}]=1;
    previous[{12,12}]=2; // Separate area must remain separate.
    previous[{-1,0}]=9; // Another region ID must remain separate.
    std::vector<Cell> stroke;
    for(int y=0;y<5;++y) for(int x=3;x<5;++x) stroke.emplace_back(x,y);
    auto read=[&](int x,int y){auto it=previous.find({x,y});return it==previous.end()?0:it->second;};
    const auto merged=core::mergedRegionArea(stroke,1,5,read);
    assert(merged.size()==25);
    const auto values=core::regionAreaGradient(merged,1,5);
    for(std::size_t i=0;i<merged.size();++i) {
        auto [x,y]=merged[i];
        assert(values[i]==1+std::min({x,y,4-x,4-y}));
    }
    assert(core::mergedRegionArea(stroke,5,1,read).size()==25);
    assert(core::mergedRegionArea(std::vector<Cell>{},1,5,read).empty());
    auto duplicate=stroke;duplicate.push_back(stroke.front());
    assert(core::mergedRegionArea(duplicate,1,5,read).size()==25);
    const auto isolated=core::mergedRegionArea(std::vector<Cell>{{20,20}},1,5,read);
    assert(isolated.size()==1);
    // A new cell bridges two existing areas, joining both in one edit.
    previous.clear();previous[{0,0}]=1;previous[{2,0}]=3;
    assert(core::mergedRegionArea(std::vector<Cell>{{1,0}},1,5,read).size()==3);
    std::cout<<"Region merge: continuity, ID range, disconnected areas, reversed range, empty, duplicates and bridge passed\n";
}
