#pragma once
#include "Types.h"

namespace ludo::core {

enum class EventExecutionOrigin { DetachedList, MapEvent, CommonEvent };
enum class EventExecutionMode { Normal, OnPageActivated, Autorun, Parallel };

struct EventExecutionContext {
    EventExecutionOrigin origin = EventExecutionOrigin::DetachedList;
    EventExecutionMode mode = EventExecutionMode::Normal;
    Text source;
    MapId mapId;
    EventId mapEventId;
    CommonEventId commonEventId;
    int commonEventNumber = 0;
    int pageIndex = -1;
    int commandIndex = -1;
    int callDepth = 0;

    bool hasMapEvent() const noexcept { return bool(mapEventId); }
    bool hasCommonEvent() const noexcept { return bool(commonEventId) || commonEventNumber > 0; }
};

} // namespace ludo::core
