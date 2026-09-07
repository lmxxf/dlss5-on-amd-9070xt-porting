#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include "MinHook.h"
// Read-only merged descriptor metadata. This does NOT resolve descriptors to
// resources and cannot alone establish history semantics. No GPU operations.
namespace {
struct MergedTexture {
 size_t size_in,size_out;
 ID3D12Device*device;
 D3D12_CPU_DESCRIPTOR_HANDLE texture,sampler;
 uint64_t handle;
};
static_assert(sizeof(MergedTexture)==48,"NVAPI x64 merged descriptor ABI");
using Query=void*(__cdecl*)(unsigned);
using GetMerged=int(__cdecl*)(MergedTexture*);
GetMerged original{};std::atomic<unsigned>calls{0};SRWLOCK log_lock=SRWLOCK_INIT;
constexpr wchar_t Log[]=LR"(D:\DLSSNR-Lab\logs\native-nvapi-texture-contract.txt)";
int __cdecl merged(MergedTexture*p){
 const int status=original(p);const unsigned n=++calls;
 if(n<=4096){MergedTexture snapshot{};SIZE_T got=0;
  const bool readable=p&&ReadProcessMemory(GetCurrentProcess(),p,&snapshot,sizeof(snapshot),&got)&&got==sizeof(snapshot);
  AcquireSRWLockExclusive(&log_lock);
  if(FILE*f=_wfopen(Log,L"ab")){
   fprintf(f,"pid=%lu tick=%llu call=%u status=%d readable=%u",GetCurrentProcessId(),GetTickCount64(),n,status,unsigned(readable));
   if(readable)fprintf(f," size=%zu,%zu device=%p texture_desc=%llx sampler_desc=%llx merged_handle=%llx",snapshot.size_in,snapshot.size_out,snapshot.device,(unsigned long long)snapshot.texture.ptr,(unsigned long long)snapshot.sampler.ptr,(unsigned long long)snapshot.handle);
   fputc('\n',f);fclose(f);
  }ReleaseSRWLockExclusive(&log_lock);
 }return status;
}
DWORD WINAPI worker(void*){
 HMODULE module=nullptr;for(unsigned i=0;i<600&&!module;i++){module=GetModuleHandleW(L"nvapi64.dll");if(!module)Sleep(100);}
 if(!module)return 1;auto query=reinterpret_cast<Query>(GetProcAddress(module,"nvapi_QueryInterface"));if(!query)return 2;
 void*target=query(0x329fe6e0);if(!target)return 3;
 auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return 4;
 status=MH_CreateHook(target,reinterpret_cast<void*>(&merged),reinterpret_cast<void**>(&original));
 if(status==MH_OK)status=MH_EnableHook(target);
 if(FILE*f=_wfopen(Log,L"ab")){fprintf(f,"pid=%lu hook_status=%u target=%p metadata_only=1\n",GetCurrentProcessId(),unsigned(status),target);fclose(f);}
 return status==MH_OK?0:5;
}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
  HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
 }return TRUE;
}
