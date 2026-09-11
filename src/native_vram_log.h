#pragma once
// VRAM accounting (DLSS5_VRAM_LOG=1): prints the adapter's dedicated-memory usage delta after each network component is created.
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include "native_pso.h"
inline void NativeVramLog(ID3D12Device*d,const char*label){
 static bool on=[]{const wchar_t*v=_wgetenv(L"DLSS5_VRAM_LOG");return v&&!wcscmp(v,L"1");}();NativeInitTick(label);if(!on)return;
 static UINT64 last=0;static ULONGLONG last_tick=GetTickCount64();static unsigned last_count=0;static ULONGLONG last_pso=0;const ULONGLONG tick=GetTickCount64();IDXGIFactory4*f=nullptr;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))return;
 IDXGIAdapter3*a=nullptr;UINT64 now=0;if(SUCCEEDED(f->EnumAdapterByLuid(d->GetAdapterLuid(),IID_PPV_ARGS(&a)))){DXGI_QUERY_VIDEO_MEMORY_INFO m{};if(SUCCEEDED(a->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&m)))now=m.CurrentUsage;a->Release();}
 f->Release();printf("vram %-14s total=%6.0f MB delta=%+6.0f MB elapsed=%llu ms pso=%u/%llu ms\n",label,now/1048576.0,(double(now)-double(last))/1048576.0,(unsigned long long)(tick-last_tick),NativePsoStats::Count()-last_count,(unsigned long long)(NativePsoStats::Ms()-last_pso));last_count=NativePsoStats::Count();last_pso=NativePsoStats::Ms();fflush(stdout);last=now;last_tick=tick;
}
