#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native codec constants observer";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Payload-only codec constant candidates; no graphics commands or method hooks.";
static std::atomic<unsigned> captured{0};
static std::mutex state_mutex;
struct ListState { uint64_t pipeline{};unsigned candidate_id{}; };
static std::map<reshade::api::command_list*,ListState> lists;
static unsigned shader_count{};
static void pipeline_init(reshade::api::device*,reshade::api::pipeline_layout,uint32_t count,const reshade::api::pipeline_subobject*objects,reshade::api::pipeline p){
 if(!objects)return;
 for(uint32_t i=0;i<count;i++)if(objects[i].type==reshade::api::pipeline_subobject_type::compute_shader&&objects[i].data){
  auto&s=*static_cast<const reshade::api::shader_desc*>(objects[i].data);
  if(!s.code||!s.code_size||s.code_size>1024*1024)continue;
  auto*b=static_cast<const char*>(s.code);const char marker[]="CodecConstants";
  if(std::search(b,b+s.code_size,marker,marker+sizeof(marker)-1)==b+s.code_size)continue;
  std::lock_guard<std::mutex>guard(state_mutex);if(shader_count>=16)continue;++shader_count;
  wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\logs\codec-%lu-%llx.dxbc)",GetCurrentProcessId(),(unsigned long long)p.handle);
  if(FILE*f=_wfopen(path,L"wb")){fwrite(s.code,1,s.code_size,f);fclose(f);}
 }
}
static void pipeline_bind(reshade::api::command_list*c,reshade::api::pipeline_stage stages,reshade::api::pipeline p){
 if((uint32_t(stages)&uint32_t(reshade::api::pipeline_stage::compute_shader))==0)return;
 std::lock_guard<std::mutex>guard(state_mutex);lists[c].pipeline=p.handle;
}
static void list_reset(reshade::api::command_list*c){std::lock_guard<std::mutex>guard(state_mutex);lists.erase(c);}
static bool dispatch(reshade::api::command_list*c,uint32_t x,uint32_t y,uint32_t z){
 std::lock_guard<std::mutex>guard(state_mutex);auto it=lists.find(c);
 if(it!=lists.end()&&it->second.candidate_id){
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-codec-constants.txt)",L"ab")){
   fprintf(f,"dispatch pid=%lu candidate=%u list=%p pipeline=%llx groups=%u,%u,%u observation_only=1\n",GetCurrentProcessId(),it->second.candidate_id,c,(unsigned long long)it->second.pipeline,x,y,z);fclose(f);
  }it->second.candidate_id=0;
 }return false;
}
static bool candidate(uint32_t first,uint32_t count,const void*values){
 if(first||count!=16||!values)return false;
 uint32_t words[16];std::memcpy(words,values,sizeof(words));
 // Layout found in installed reference addon's embedded CodecConstants.
 // Shape alone is NOT proof of shader identity. Preserve raw words for review.
 return words[0]==1920&&words[1]==1080&&words[2]>0&&words[2]<=7680&&
        words[3]>0&&words[3]<=4320&&words[6]>0&&words[6]<=7680&&
        words[7]>0&&words[7]<=4320&&words[11]<=2;
}
static void constants(reshade::api::command_list*c,reshade::api::shader_stage stages,
 reshade::api::pipeline_layout layout,uint32_t param,uint32_t first,uint32_t count,const void*values){
 // A later constants write invalidates a pending candidate; do not associate
 // stale values with a subsequent dispatch after partial/unrelated updates.
 {std::lock_guard<std::mutex>guard(state_mutex);auto it=lists.find(c);if(it!=lists.end())it->second.candidate_id=0;}
 if(!candidate(first,count,values))return;
 unsigned n=++captured;if(n>32)return;
 {std::lock_guard<std::mutex>guard(state_mutex);lists[c].candidate_id=n;}
 uint32_t words[16];std::memcpy(words,values,sizeof(words));
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-codec-constants.txt)",L"ab")){
  fprintf(f,"candidate pid=%lu tick=%llu call=%u list=%p stages=%u layout=%llx param=%u words=",GetCurrentProcessId(),GetTickCount64(),n,c,unsigned(stages),(unsigned long long)layout.handle,param);
  for(auto word:words)fprintf(f,"%08x,",word);
  fprintf(f," shader_identity_verified=0\n");fclose(f);
 }
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  if(!wcsstr(path,L"SB-Win64-Shipping.exe")||!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::push_constants>(constants);
  reshade::register_event<reshade::addon_event::init_pipeline>(pipeline_init);
  reshade::register_event<reshade::addon_event::bind_pipeline>(pipeline_bind);
  reshade::register_event<reshade::addon_event::dispatch>(dispatch);
  reshade::register_event<reshade::addon_event::reset_command_list>(list_reset);
  reshade::register_event<reshade::addon_event::destroy_command_list>(list_reset);
 }else if(reason==DLL_PROCESS_DETACH){
  reshade::unregister_event<reshade::addon_event::push_constants>(constants);
  reshade::unregister_event<reshade::addon_event::init_pipeline>(pipeline_init);
  reshade::unregister_event<reshade::addon_event::bind_pipeline>(pipeline_bind);
  reshade::unregister_event<reshade::addon_event::dispatch>(dispatch);
  reshade::unregister_event<reshade::addon_event::reset_command_list>(list_reset);
  reshade::unregister_event<reshade::addon_event::destroy_command_list>(list_reset);
  reshade::unregister_addon(h);
 }return TRUE;
}
