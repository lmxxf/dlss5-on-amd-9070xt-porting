#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "reshade.hpp"
#include "MinHook.h"
extern "C" __declspec(dllexport) const char*NAME="Native FFX submission order observer";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Records FFX and command-list ordering; no GPU work or command mutation.";
struct Header {uint64_t type;Header*next;};
using Dispatch=uint32_t(*)(void**,const Header*);
static Dispatch original{};
static std::atomic<unsigned> frames{},events{};
static SRWLOCK lock=SRWLOCK_INIT;
static void log(const char*kind,void*list,void*queue,unsigned value=0){
 if(!frames.load()||events.fetch_add(1)>=8192)return;
 AcquireSRWLockExclusive(&lock);
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
  fprintf(f,"pid=%lu thread=%lu tick=%llu kind=%s list=%p queue=%p value=%u observation_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),kind,list,queue,value);fclose(f);
 }ReleaseSRWLockExclusive(&lock);
}
static uint32_t dispatch(void**context,const Header*h){
 if(!h||(h->type&0x00ffffffu)!=0x00010001u)return original(context,h);
 // Existing FFX x64 ABI: commandList follows the16-byte header. Payload only.
 void*list=nullptr;SIZE_T got=0;
 ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+16,&list,sizeof(list),&got);
 unsigned n=++frames;log("ffx_begin",got==sizeof(list)?list:nullptr,nullptr,n);
 auto result=original(context,h);log("ffx_end",list,nullptr,result);return result;
}
static void close_list(reshade::api::command_list*c){log("close_api",c,nullptr);log("close_native",reinterpret_cast<void*>(c->get_native()),nullptr);}
static void execute(reshade::api::command_queue*q,reshade::api::command_list*c){log("before_execute_api",c,q);log("before_execute_native",reinterpret_cast<void*>(c->get_native()),reinterpret_cast<void*>(q->get_native()));}
static bool compute(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t){log("dispatch_api",c,nullptr);return false;}
static bool draw(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t,uint32_t){log("draw_api",c,nullptr);return false;}
static DWORD WINAPI worker(void*){
 HMODULE module=nullptr;for(unsigned i=0;i<600&&!module;i++){module=GetModuleHandleW(L"amd_fidelityfx_dx12.dll");if(!module)Sleep(100);}if(!module)return 1;
 auto target=GetProcAddress(module,"ffxDispatch");if(!target)return 2;
 auto s=MH_Initialize();if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED)return 3;
 s=MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&dispatch),reinterpret_cast<void**>(&original));if(s==MH_OK)s=MH_EnableHook(reinterpret_cast<void*>(target));
 // Do not retry an existing-hook conflict or modify another addon's hook.
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu hook_status=%u\n",GetCurrentProcessId(),unsigned(s));fclose(f);}return s==MH_OK?0:4;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  if(!wcsstr(path,L"SB-Win64-Shipping.exe")||!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::close_command_list>(close_list);
  reshade::register_event<reshade::addon_event::execute_command_list>(execute);
  reshade::register_event<reshade::addon_event::dispatch>(compute);
  reshade::register_event<reshade::addon_event::draw>(draw);
  HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
  HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
 }return TRUE;
}
