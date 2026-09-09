#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <unordered_set>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native DLSS5 Resource View Events (read-only)";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Observes ReShade view initialization without replacing device or NGX methods.";
static SRWLOCK log_lock=SRWLOCK_INIT;
static std::atomic<unsigned> count{0};
static std::atomic<unsigned> update_count{0};
static std::atomic<unsigned> copy_count{0};
static std::unordered_set<uint64_t> tracked_resources;
static unsigned resource_copy_count=0;
static void resource_destroy(reshade::api::device*,reshade::api::resource r){
 AcquireSRWLockExclusive(&log_lock);tracked_resources.erase(r.handle);ReleaseSRWLockExclusive(&log_lock);
}
static void box_log(FILE*f,const char*name,const reshade::api::subresource_box*b){
 if(b)fprintf(f," %s=%u,%u,%u,%u,%u,%u",name,b->left,b->top,b->front,b->right,b->bottom,b->back);else fprintf(f," %s=full",name);
}
static void resource_copy_log(const char*kind,reshade::api::command_list*c,reshade::api::resource source,reshade::api::resource dest,uint32_t ss,uint32_t ds,const reshade::api::subresource_box*sb,const reshade::api::subresource_box*db){
 AcquireSRWLockExclusive(&log_lock);
 if(resource_copy_count<8192&&(tracked_resources.count(source.handle)||tracked_resources.count(dest.handle))){
  unsigned n=++resource_copy_count;
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-resource-view-events.txt)",L"ab")){
   fprintf(f,"resource_copy pid=%lu tick=%llu call=%u kind=%s list=%p source=%llx dest=%llx source_sub=%u dest_sub=%u",GetCurrentProcessId(),GetTickCount64(),n,kind,c,(unsigned long long)source.handle,(unsigned long long)dest.handle,ss,ds);box_log(f,"source_box",sb);box_log(f,"dest_box",db);fprintf(f," observation_only=1\n");fclose(f);
  }
 }ReleaseSRWLockExclusive(&log_lock);
}
static bool resource_copy(reshade::api::command_list*c,reshade::api::resource source,reshade::api::resource dest){
 resource_copy_log("resource",c,source,dest,0,0,nullptr,nullptr);return false;
}
static bool texture_copy(reshade::api::command_list*c,reshade::api::resource source,uint32_t ss,const reshade::api::subresource_box*sb,reshade::api::resource dest,uint32_t ds,const reshade::api::subresource_box*db,reshade::api::filter_mode){
 resource_copy_log("texture",c,source,dest,ss,ds,sb,db);return false;
}
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
 AcquireSRWLockExclusive(&log_lock);tracked_resources.insert(resource.handle);ReleaseSRWLockExclusive(&log_lock);
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
  reshade::register_event<reshade::addon_event::destroy_resource>(resource_destroy);
  reshade::register_event<reshade::addon_event::copy_resource>(resource_copy);
  reshade::register_event<reshade::addon_event::copy_texture_region>(texture_copy);
 }else if(reason==DLL_PROCESS_DETACH){
  reshade::unregister_event<reshade::addon_event::init_resource_view>(view_init);
  reshade::unregister_event<reshade::addon_event::update_descriptor_tables>(table_update);
  reshade::unregister_event<reshade::addon_event::copy_descriptor_tables>(table_copy);
  reshade::unregister_event<reshade::addon_event::destroy_resource>(resource_destroy);
  reshade::unregister_event<reshade::addon_event::copy_resource>(resource_copy);
  reshade::unregister_event<reshade::addon_event::copy_texture_region>(texture_copy);
  reshade::unregister_addon(h);
 }return TRUE;
}
