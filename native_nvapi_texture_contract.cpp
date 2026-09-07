#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include "MinHook.h"
// Read-only merged descriptor/SRV creation metadata. Descriptor copies and
// creations before hook installation are not observed; never infer missing
// resource mappings or history semantics from absence. No GPU submissions.
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
using CreateSrv=void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
CreateSrv original_srv{};std::atomic<unsigned>srv_calls{0};std::once_flag srv_once;
void STDMETHODCALLTYPE create_srv(ID3D12Device*d,ID3D12Resource*r,const D3D12_SHADER_RESOURCE_VIEW_DESC*view,D3D12_CPU_DESCRIPTOR_HANDLE handle){
 original_srv(d,r,view,handle);
 unsigned n=++srv_calls;if(n>4096)return;
 D3D12_RESOURCE_DESC desc{};if(r)desc=r->GetDesc();
 AcquireSRWLockExclusive(&log_lock);
 if(FILE*f=_wfopen(Log,L"ab")){
  fprintf(f,"srv_create pid=%lu tick=%llu call=%u device=%p desc=%llx resource=%p size=%llux%u format=%u view_format=%u view_dimension=%u\n",GetCurrentProcessId(),GetTickCount64(),n,d,(unsigned long long)handle.ptr,r,(unsigned long long)desc.Width,desc.Height,unsigned(desc.Format),view?unsigned(view->Format):0,view?unsigned(view->ViewDimension):0);fclose(f);
 }ReleaseSRWLockExclusive(&log_lock);
}
void install_srv(ID3D12Device*d){
 std::call_once(srv_once,[&](){
  // Index18 confirmed against ID3D12DeviceVtbl in the Windows SDK header.
  void*target=(*reinterpret_cast<void***>(d))[18];
  auto status=MH_CreateHook(target,reinterpret_cast<void*>(&create_srv),reinterpret_cast<void**>(&original_srv));
  if(status==MH_OK)status=MH_EnableHook(target);
  AcquireSRWLockExclusive(&log_lock);
  if(FILE*f=_wfopen(Log,L"ab")){fprintf(f,"srv_hook pid=%lu status=%u target=%p late_install=1\n",GetCurrentProcessId(),unsigned(status),target);fclose(f);}
  ReleaseSRWLockExclusive(&log_lock);
 });
}
int __cdecl merged(MergedTexture*p){
 const unsigned n=++calls;
 if(n<=4096){MergedTexture before{};SIZE_T got=0;
  if(p&&ReadProcessMemory(GetCurrentProcess(),p,&before,sizeof(before),&got)&&got==sizeof(before)){
   AcquireSRWLockExclusive(&log_lock);
   if(FILE*f=_wfopen(Log,L"ab")){fprintf(f,"merged_enter pid=%lu tick=%llu call=%u device=%p texture_desc=%llx sampler_desc=%llx\n",GetCurrentProcessId(),GetTickCount64(),n,before.device,(unsigned long long)before.texture.ptr,(unsigned long long)before.sampler.ptr);fclose(f);}
   ReleaseSRWLockExclusive(&log_lock);
  }
 }
 const int status=original(p);
 if(n<=4096){MergedTexture snapshot{};SIZE_T got=0;
  const bool readable=p&&ReadProcessMemory(GetCurrentProcess(),p,&snapshot,sizeof(snapshot),&got)&&got==sizeof(snapshot);
  if(status==0&&readable&&snapshot.size_in>=sizeof(snapshot)&&snapshot.device)install_srv(snapshot.device);
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
