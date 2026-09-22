#include "core/EditorInputPolicy.h"
#include <iostream>
int main(){
 for(bool objects:{false,true})for(bool control:{false,true})for(bool shift:{false,true}){
  auto result=core::heldEditorModes(objects,control,shift);
  if(result.snap!=(objects&&control)||result.erase!=(!objects&&control)||result.stack!=(!objects&&shift))return 1;
 }
 int index=-1;index=core::objectDepthBack(index,4);if(index!=3)return 2;
 if(core::objectInsertionPosition(index,4)!=3||core::objectInsertionPosition(index,5)!=3)return 3;
 index=core::objectDepthForward(index,5);if(index!=4)return 4;
 if(core::objectInsertionPosition(-1,6)!=6||core::objectDepthBack(-1,0)!=0||core::objectDepthBack(0,8)!=0)return 5;
 std::cout<<"8 modifier combinations and persistent depth insertion passed\n";
}
