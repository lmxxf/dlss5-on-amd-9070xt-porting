#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native DLSS5 Resource View Events (read-only)";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Observes ReShade view initialization without replacing device or NGX methods.";
static SRWLOCK log_lock=SRWLOCK_INIT;
static std::atomic<unsigned> count{0};
static void view_init(reshade::api::device*d,reshade::api::resource resource,reshade::api::resource_usage usage,const reshade::api::resource_view_desc&view_desc,reshade::api::resource_view view){
 // Do not call device virtual methods during initialization. Payload only;
 // dimensions and native-device identity must be established separately.
 if(!d||!resource.handle)return;
 if(view_desc.format!=reshade::api::format::r16g16b16a16_float&&view_desc.format!=reshade::api::format::r16g16_float)return;
 const unsigned n=++count;if(n>8192)return;
 AcquireSRWLockExclusive(&log_lock);
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-resource-view-events.txt)",L"ab")){
  fprintf(f,"view_init pid=%lu tick=%llu call=%u api_device=%p resource=%llx view=%llx usage=%u view_format=%u view_type=%u payload_only=1\n",GetCurrentProcessId(),GetTickCount64(),n,d,(unsigned long long)resource.handle,(unsigned long long)view.handle,unsigned(usage),unsigned(view_desc.format),unsigned(view_desc.type));fclose(f);
 }ReleaseSRWLockExclusive(&log_lock);
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  if(!wcsstr(path,L"SB-Win64-Shipping.exe")||!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::init_resource_view>(view_init);
 }else if(reason==DLL_PROCESS_DETACH){
  reshade::unregister_event<reshade::addon_event::init_resource_view>(view_init);
  reshade::unregister_addon(h);
 }return TRUE;
}
