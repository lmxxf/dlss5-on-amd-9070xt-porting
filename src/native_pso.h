#pragma once
// Every compute PSO of the network goes through here: counts and times the driver's DXIL->ISA compile (reported by NativeVramLog
// per component when DLSS5_VRAM_LOG=1). Also the single place for a future on-disk PSO cache.
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
struct NativePsoStats{static ULONGLONG&Ms(){static ULONGLONG v=0;return v;}static unsigned&Count(){static unsigned c=0;return c;}};
inline HRESULT NativeCreateComputePipelineState(ID3D12Device*d,const D3D12_COMPUTE_PIPELINE_STATE_DESC*pd,REFIID iid,void**pso){
 const ULONGLONG t=GetTickCount64();HRESULT h=d->CreateComputePipelineState(pd,iid,pso);NativePsoStats::Ms()+=GetTickCount64()-t;NativePsoStats::Count()++;return h;}
// Fine-grained init timing (DLSS5_VRAM_LOG=1): prints the milliseconds since the previous checkpoint.
// DLSS5_INIT_LOG=<file>: the same checkpoints appended to a file (the add-on has no stdout).
inline void NativeInitTick(const char*label){
 /* env is re-read until set: the flags file is applied after the first checkpoints (noise read) */
 static bool on=false;static std::wstring file;if(!on){const wchar_t*v=_wgetenv(L"DLSS5_VRAM_LOG");on=v&&!wcscmp(v,L"1");}if(file.empty()){const wchar_t*v=_wgetenv(L"DLSS5_INIT_LOG");if(v)file=v;}if(!on&&file.empty())return;
 static LARGE_INTEGER freq=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f;}();static LARGE_INTEGER last=[]{LARGE_INTEGER c;QueryPerformanceCounter(&c);return c;}();
 LARGE_INTEGER t;QueryPerformanceCounter(&t);const double ms=double(t.QuadPart-last.QuadPart)*1000.0/double(freq.QuadPart);
 if(on){printf("  tick %-28s +%.2f ms\n",label,ms);fflush(stdout);}
 if(!file.empty()){static FILE*f=nullptr;if(!f)f=_wfopen(file.c_str(),L"ab");if(f){fprintf(f,"tick %-28s +%.2f ms\n",label,ms);fflush(f);}} /* kept open: an fopen/fclose per line cost ~5 ms each on Windows */
 last=t;}
