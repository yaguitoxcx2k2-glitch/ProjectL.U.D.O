#include "core/PublicationPolicy.h"
#include <cassert>
#include <iostream>
int main() {
    using core::publicationMapId;
    const std::set<int> existing{1,2,4,9};
    assert(publicationMapId(0,0,existing,{})==3);
    assert(publicationMapId(0,0,existing,{3})==5);
    assert(publicationMapId(9,2,existing,{})==9);
    assert(publicationMapId(0,2,existing,{})==2);
    assert(publicationMapId(9,0,existing,{9})==-1);
    assert(publicationMapId(0,2,existing,{2})==-1);
    // The publication UI repairs duplicate/foreign bindings instead of
    // aborting the whole export. Existing RPG Maker maps stay occupied, while
    // a safe free ID is selected for the conflicting LUDO map.
    assert(core::reconciledPublicationMapId(9,0,existing,{9},{})==3);
    assert(core::reconciledPublicationMapId(0,2,existing,{},std::set<int>{2})==3);
    assert(core::reconciledPublicationMapId(9,0,existing,{},std::set<int>{9})==3);
    assert(core::reconciledPublicationMapId(9,0,existing,{},std::set<int>{})==9);
    assert(!core::publicationChanged(true,true,true));
    assert(core::publicationChanged(false,true,true));
    assert(core::publicationChanged(true,false,true));
    assert(core::publicationChanged(true,true,false));
    std::cout<<"Publication: ID allocation, stable mappings, collisions, changes and missing outputs passed\n";
}
