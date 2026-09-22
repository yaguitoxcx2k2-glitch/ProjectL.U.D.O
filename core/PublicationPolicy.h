#pragma once
#include <set>
namespace core {
// Preserve a known mapping. New maps use an unoccupied ID.
inline int publicationMapId(int previous,int bound,const std::set<int>& occupied,const std::set<int>& assigned) {
    const int preferred=previous>0?previous:bound;
    if(preferred>0)return assigned.count(preferred)?-1:preferred;
    for(int id=1;id<=999999;++id)if(!occupied.count(id)&&!assigned.count(id))return id;
    return -1;
}

// Publication flow: preserve a known mapping when it is safe; if a duplicated
// binding or a foreign LUDO project owns the preferred target, repair the
// mapping by selecting the next truly free Map ID instead of blocking the
// entire publication. `blocked` is distinct from `occupied`: occupied Map IDs
// may be legitimate update targets for this project, while blocked IDs may not.
inline int reconciledPublicationMapId(int previous,int bound,const std::set<int>& occupied,
                                      const std::set<int>& assigned,const std::set<int>& blocked) {
    const int preferred=previous>0?previous:bound;
    if(preferred>0 && !assigned.count(preferred) && !blocked.count(preferred))return preferred;
    for(int id=1;id<=999999;++id)
        if(!occupied.count(id)&&!assigned.count(id)&&!blocked.count(id))return id;
    return -1;
}
inline bool publicationChanged(bool sameFingerprint,bool mapExists,bool manifestExists) {
    return !sameFingerprint || !mapExists || !manifestExists;
}
}
