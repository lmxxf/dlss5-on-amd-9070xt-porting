#include "native_snapshot_gate.h"
#include <cstdio>
int main(){
 uintptr_t good[]={1,2},duplicate[]={2,2};
 if(!NativeSnapshotBatchMatch(7,7,2,good,2))return 1;
 if(NativeSnapshotBatchMatch(7,8,2,good,2)||NativeSnapshotBatchMatch(7,7,1,good,2)||NativeSnapshotBatchMatch(7,7,2,duplicate,2)||NativeSnapshotBatchMatch(7,7,0,good,2)||NativeSnapshotBatchMatch(7,7,2,nullptr,2)||NativeSnapshotBatchMatch(7,7,2,good,0)||NativeSnapshotBatchMatch(7,7,2,good,65))return 2;
 puts("snapshot gate: last-list, unique-list, same-thread and bounds pass");return 0;
}
