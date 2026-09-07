#pragma once
#include <cstdint>
#include <cstddef>
inline bool NativeSnapshotBatchMatch(uint32_t producer_thread,uint32_t current_thread,
 uintptr_t producer_list,const uintptr_t*lists,size_t count){
 if(!producer_list||!lists||!count||count>64||producer_thread!=current_thread)return false;
 if(lists[count-1]!=producer_list)return false;
 for(size_t i=0;i+1<count;i++)if(lists[i]==producer_list)return false;
 return true;
}
