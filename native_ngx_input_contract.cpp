#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include "nvsdk_ngx_params.h"
#include "native_ngx_parameter_access.h"
#include "MinHook.h"
// Original DLSSNR Evaluate entry only; reads parameter metadata, not pixels.
namespace {
using Evaluate=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
Evaluate original{};std::atomic<unsigned>calls{0};
constexpr auto logpath=LR"(D:\DLSSNR-Lab\logs\native-ngx-input-contract.txt)";
NVSDK_NGX_Result NVSDK_CONV evaluate(ID3D12GraphicsCommandList*c,const NVSDK_NGX_Handle*h,const NVSDK_NGX_Parameter*p,PFN_NVSDK_NGX_ProgressCallback callback){
 unsigned n=++calls;if(p&&(n<=3||n%600==0))if(FILE*f=_wfopen(logpath,L"ab")){
  std::fprintf(f,"pid=%lu evaluate=%u list=%p handle=%p\n",GetCurrentProcessId(),n,c,h);
  for(const char*key:{"Width","Height","OutWidth","OutHeight","DLSSNR.ColorSubrectBaseX","DLSSNR.ColorSubrectBaseY","DLSSNR.ColorSubrectWidth","DLSSNR.ColorSubrectHeight","DLSSNR.OutputSubrectWidth","DLSSNR.OutputSubrectHeight"}){unsigned value=0;auto result=NativeNgxGetUInt(p,key,&value);std::fprintf(f,"scalar=%s status=%08x value=%u\n",key,unsigned(result),value);}
  // Names verified in the original DLSSNR binary, not inferred from DLSS SR.
  for(const char*key:{"DLSSNR.Color","DLSSNR.Output","DLSSNR.Depth","DLSSNR.MVec","DLSSNR.ControlMask","DLSSNR.UI","DLSSNR.UIAlpha","DLSSNR.Backbuffer"}){
   ID3D12Resource*r=nullptr;auto result=NativeNgxGetResource(p,key,&r);std::fprintf(f,"resource=%s status=%08x pointer=%p",key,unsigned(result),r);
   if(result==NVSDK_NGX_Result_Success&&r){auto d=r->GetDesc();std::fprintf(f," dimension=%u size=%llux%u format=%u flags=%u",unsigned(d.Dimension),static_cast<unsigned long long>(d.Width),d.Height,unsigned(d.Format),unsigned(d.Flags));}
   std::fputc('\n',f);
  }std::fclose(f);
 }
 return original(c,h,p,callback);
}
DWORD WINAPI worker(void*){
 HMODULE module=nullptr;for(unsigned i=0;i<600&&!module;i++){module=GetModuleHandleW(L"nvngx_dlssnr.dll");if(!module)Sleep(100);}
 if(!module)return 1;auto target=GetProcAddress(module,"NVSDK_NGX_D3D12_EvaluateFeature");if(!target)return 2;
 auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return 3;
 status=MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&evaluate),reinterpret_cast<void**>(&original));if(status==MH_OK)status=MH_EnableHook(reinterpret_cast<void*>(target));
 if(FILE*f=_wfopen(logpath,L"ab")){std::fprintf(f,"pid=%lu hook_status=%u target=%p read_only_parameters=1\n",GetCurrentProcessId(),unsigned(status),target);std::fclose(f);}return status==MH_OK?0:4;
}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){
 DisableThreadLibraryCalls(h);HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
 HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
}return TRUE;}
