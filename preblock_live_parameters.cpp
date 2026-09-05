#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
// CPU parameter capture only: no resource mutation, GPU injection or frame edits.
namespace {
using Launch=int64_t(*)(void*,void*,uint32_t,uint32_t,uint32_t,void*,uint64_t,uint8_t);
Launch original=nullptr;std::atomic<unsigned> count{0};
constexpr wchar_t Log[]=LR"(D:\DLSSNR-Lab\logs\preblock-live-parameters.txt)";
int64_t hook(void*self,void*context,uint32_t x,uint32_t y,uint32_t z,void*blob,uint64_t bytes,uint8_t flag){
 if(bytes==0x108&&blob){unsigned n=count.fetch_add(1);if(n<8){
  unsigned char data[0x108]{};SIZE_T got=0;void*parameters=nullptr;
  // Backend +449a0 loads [argument_array] before forwarding the by-value blob.
  if(ReadProcessMemory(GetCurrentProcess(),blob,&parameters,sizeof(parameters),&got)&&got==sizeof(parameters)&&parameters&&
     ReadProcessMemory(GetCurrentProcess(),parameters,data,sizeof(data),&got)&&got==sizeof(data)){
   wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\logs\preblock-live-%u.bin)",n);
   if(FILE*f=_wfopen(path,L"wb")){fwrite(data,1,sizeof(data),f);fclose(f);}
   uint32_t seed=0,w=0,h=0;memcpy(&seed,data+0xc8,4);memcpy(&w,data+0xf0,4);memcpy(&h,data+0xf4,4);
   if(FILE*f=_wfopen(Log,L"ab")){fprintf(f,"format=indirect-v2 capture=%u pid=%lu tick=%llu grid=%u,%u,%u dims=%u,%u seed=%08x flag=%u\n",n,GetCurrentProcessId(),GetTickCount64(),x,y,z,w,h,seed,unsigned(flag));fclose(f);}
  }
 }}
 return original(self,context,x,y,z,blob,bytes,flag);
}
DWORD WINAPI worker(void*){
 HMODULE module=nullptr;for(unsigned i=0;i<600&&!module;i++){module=GetModuleHandleW(L"nvngx_dlssnr.dll");if(!module)Sleep(100);}
 if(!module)return 1;auto*target=reinterpret_cast<unsigned char*>(module)+0x449a0;
 const unsigned char expected[]={0x40,0x53,0x55,0x56,0x57,0x48,0x81,0xec,0xe8,0,0,0};
 if(memcmp(target,expected,sizeof(expected))){if(FILE*f=_wfopen(Log,L"wb")){fputs("signature mismatch; no hook installed\n",f);fclose(f);}return 2;}
 const auto init=MH_Initialize();const auto create=MH_CreateHook(target,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original));
 const auto enable=create==MH_OK?MH_EnableHook(target):create;
 if(FILE*f=_wfopen(Log,L"wb")){fprintf(f,"format=indirect-v2 hook init=%d create=%d enable=%d pid=%lu\n",int(init),int(create),int(enable),GetCurrentProcessId());fclose(f);}
 return enable==MH_OK?0:3;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned);HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
