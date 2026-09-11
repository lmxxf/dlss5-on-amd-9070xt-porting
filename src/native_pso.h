#pragma once
// Every compute PSO of the network goes through here: counts and times the driver's DXIL->ISA compile (reported by NativeVramLog
// per component when DLSS5_VRAM_LOG=1). Also the single place for a future on-disk PSO cache.
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
struct NativePsoStats{static ULONGLONG&Ms(){static ULONGLONG v=0;return v;}static unsigned&Count(){static unsigned c=0;return c;}};
inline HRESULT NativeCreateComputePipelineState(ID3D12Device*d,const D3D12_COMPUTE_PIPELINE_STATE_DESC*pd,REFIID iid,void**pso){
 const ULONGLONG t=GetTickCount64();HRESULT h=d->CreateComputePipelineState(pd,iid,pso);NativePsoStats::Ms()+=GetTickCount64()-t;NativePsoStats::Count()++;return h;}
// Fine-grained init timing (DLSS5_VRAM_LOG=1): prints the milliseconds since the previous checkpoint.
// DLSS5_INIT_LOG=<file>: the same checkpoints appended to a file (the add-on has no stdout).
inline void NativeInitTick(const char*label){
 static bool on=[]{const wchar_t*v=_wgetenv(L"DLSS5_VRAM_LOG");return v&&!wcscmp(v,L"1");}();static const wchar_t*file=_wgetenv(L"DLSS5_INIT_LOG");if(!on&&!file)return;
 static ULONGLONG last=GetTickCount64();const ULONGLONG t=GetTickCount64();
 if(on){printf("  tick %-28s +%llu ms\n",label,(unsigned long long)(t-last));fflush(stdout);}
 if(file){if(FILE*f=_wfopen(file,L"ab")){fprintf(f,"tick %-28s +%llu ms\n",label,(unsigned long long)(t-last));fclose(f);}}
 last=t;}
