#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "reshade.hpp"
extern "C" __declspec(dllexport) const char*NAME="Native codec constants observer";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Payload-only codec constant candidates; no graphics commands or method hooks.";
static std::atomic<unsigned> captured{0};
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
 if(!candidate(first,count,values))return;
 unsigned n=++captured;if(n>32)return;
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
 }else if(reason==DLL_PROCESS_DETACH){
  reshade::unregister_event<reshade::addon_event::push_constants>(constants);reshade::unregister_addon(h);
 }return TRUE;
}
