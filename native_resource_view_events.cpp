#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native DLSS5 Resource View Events (read-only)";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Observes ReShade view initialization without replacing device or NGX methods.";
static SRWLOCK log_lock=SRWLOCK_INIT;
static std::atomic<unsigned> count{0};
static std::atomic<unsigned> update_count{0};
static std::atomic<unsigned> copy_count{0};
static bool table_copy(reshade::api::device*d,uint32_t count,const reshade::api::descriptor_table_copy*copies){
 if(!copies)return false;
 for(uint32_t i=0;i<count;i++){
  unsigned n=++copy_count;if(n>16384)return false;const auto&c=copies[i];
  AcquireSRWLockExclusive(&log_lock);
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-resource-view-events.txt)",L"ab")){
   fprintf(f,"descriptor_copy pid=%lu tick=%llu call=%u api_device=%p source=%llx source_binding=%u source_array=%u dest=%llx dest_binding=%u dest_array=%u count=%u observation_only=1\n",GetCurrentProcessId(),GetTickCount64(),n,d,(unsigned long long)c.source_table.handle,c.source_binding,c.source_array_offset,(unsigned long long)c.dest_table.handle,c.dest_binding,c.dest_array_offset,c.count);fclose(f);
  }ReleaseSRWLockExclusive(&log_lock);
 }
 return false;
}
static bool table_update(reshade::api::device*d,uint32_t count,const reshade::api::descriptor_table_update*updates){
 if(!updates)return false;
 for(uint32_t i=0;i<count;i++){
  const auto&u=updates[i];
  if(u.type!=reshade::api::descriptor_type::texture_shader_resource_view&&u.type!=reshade::api::descriptor_type::texture_unordered_access_view)continue;
  if(!u.descriptors)continue;
  const auto*views=static_cast<const reshade::api::resource_view*>(u.descriptors);
  for(uint32_t j=0;j<u.count;j++){
   unsigned n=++update_count;if(n>8192)return false;
   AcquireSRWLockExclusive(&log_lock);
   if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-resource-view-events.txt)",L"ab")){
    fprintf(f,"descriptor_update pid=%lu tick=%llu call=%u api_device=%p table=%llx binding=%u array_offset=%u element=%u count=%u type=%u view=%llx observation_only=1\n",GetCurrentProcessId(),GetTickCount64(),n,d,(unsigned long long)u.table.handle,u.binding,u.array_offset,j,u.count,unsigned(u.type),(unsigned long long)views[j].handle);fclose(f);
   }ReleaseSRWLockExclusive(&log_lock);
  }
 }
 return false; // Never suppress or replace the original descriptor update.
}
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
  reshade::register_event<reshade::addon_event::update_descriptor_tables>(table_update);
  reshade::register_event<reshade::addon_event::copy_descriptor_tables>(table_copy);
 }else if(reason==DLL_PROCESS_DETACH){
  reshade::unregister_event<reshade::addon_event::init_resource_view>(view_init);
  reshade::unregister_event<reshade::addon_event::update_descriptor_tables>(table_update);
  reshade::unregister_event<reshade::addon_event::copy_descriptor_tables>(table_copy);
  reshade::unregister_addon(h);
 }return TRUE;
}
