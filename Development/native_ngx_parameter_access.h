#pragma once
#include "nvsdk_ngx_params.h"
// NVSDK_NGX_Parameter is built with Microsoft's ABI. MS vtables group overloads
// and reverse declarations within each group (LLVM VTableBuilder.cpp,
// GroupNewVirtualOverloads). The official header has 8 Set, 8 Get, then Reset.
// MinGW's normal C++ calls use declaration order instead; use explicit x64 ABI.
static_assert(sizeof(void*)==8,"NGX parameter bridge is x64 only");
inline NVSDK_NGX_Result NativeNgxGetResource(const NVSDK_NGX_Parameter*p,const char*key,ID3D12Resource**out){
 using Fn=NVSDK_NGX_Result(NVSDK_CONV*)(const NVSDK_NGX_Parameter*,const char*,ID3D12Resource**);
 auto table=*reinterpret_cast<void*const*const*>(p);return reinterpret_cast<Fn>(table[9])(p,key,out);
}
inline NVSDK_NGX_Result NativeNgxGetUInt(const NVSDK_NGX_Parameter*p,const char*key,unsigned*out){
 using Fn=NVSDK_NGX_Result(NVSDK_CONV*)(const NVSDK_NGX_Parameter*,const char*,unsigned*);
 auto table=*reinterpret_cast<void*const*const*>(p);return reinterpret_cast<Fn>(table[12])(p,key,out);
}
