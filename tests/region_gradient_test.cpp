#include "core/RegionGradient.h"
#include <iostream>
#include <set>
#include <stdexcept>
using Cell = std::pair<int,int>;
void check(const std::vector<Cell>& cells, const std::vector<int>& expected, int start=1, int end=5) {
    if (core::regionAreaGradient(cells,start,end) != expected) throw std::runtime_error("unexpected region values");
}
int main() {
    std::vector<Cell> square;
    std::vector<int> values;
    for (int y=0;y<9;++y) for (int x=0;x<13;++x) {
        square.emplace_back(x,y);
        values.push_back(1+std::min({x,12-x,y,8-y,4}));
    }
    check(square,values); // Rectangle stays a rectangle, including all four corners.
    std::reverse(square.begin(),square.end()); std::reverse(values.begin(),values.end());
    check(square,values); // Independent of traversal/hash order.
    auto reverseValues=values; for(int& value:reverseValues)value=6-value;
    check(square,reverseValues,5,1);
    std::vector<Cell> diamond;
    std::vector<int> diamondValues;
    for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x)if(std::abs(x)+std::abs(y)<=4){
        diamond.emplace_back(x,y);diamondValues.push_back(5-std::abs(x)-std::abs(y));
    }
    check(diamond,diamondValues); // Reference distribution, only when the input area has this shape.
    std::vector<Cell> hole; std::vector<int> holeValues;
    for(int y=0;y<9;++y)for(int x=0;x<9;++x)if(x!=4||y!=4){
        hole.emplace_back(x,y);
        holeValues.push_back(1+std::min({x,8-x,y,8-y,std::abs(x-4)+std::abs(y-4)-1}));
    }
    check(hole,holeValues);
    check({{0,0},{1,0},{2,0},{0,1},{0,2},{8,8}}, {1,1,1,1,1,1}); // Irregular and disconnected.
    check({{0,0},{1,0},{2,0}}, {1,1,1}); // Thin area cannot reach the interior ID.
    check({},{});
    check({{0,0},{0,0}}, {3,3},3,3);
    check({{0,0}}, {254},254,255);
    for(int i=0;i<9;++i)if(core::regionGradientValue(1,5,i,false)!=std::min(5,1+i))return 2;
    std::cout << "Area gradients: rectangle, ordering, reverse, reference, hole, irregular/disconnected, thin, empty, duplicate and bounds passed; linear unchanged.\n";
}
