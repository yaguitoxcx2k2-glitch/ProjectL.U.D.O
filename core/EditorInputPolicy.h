#pragma once
#include <algorithm>
namespace core {
struct HeldEditorModes { bool stack, snap, erase; };
inline HeldEditorModes heldEditorModes(bool objects, bool control, bool shift) {
    return {!objects && shift, objects && control, !objects && control};
}
inline int objectDepthBack(int index,int count) { return std::max(0,(index<0?count:std::min(index,count))-1); }
inline int objectDepthForward(int index,int count) { return index<0||index>=count?-1:index+1; }
inline int objectInsertionPosition(int index,int count) { return index<0?count:std::clamp(index,0,count); }
}
