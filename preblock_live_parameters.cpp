#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "MinHook.h"
// CPU parameter capture only: no resource mutation, GPU injection or frame edits.
namespace {
using Launch=int64_t(*)(void*,void*,uint32_t,uint32_t,uint32_t,void*,uint64_t,uint8_t);
Launch original=nullptr;std::atomic<unsigned> count{0};
using GetKernel=void*(*)(void*,const char*,uint32_t,uint32_t,uint32_t,uint64_t);
GetKernel original_get=nullptr;SRWLOCK names_lock=SRWLOCK_INIT;
std::vector<std::pair<void*,std::string>> names;std::atomic<unsigned> launches{0};
constexpr wchar_t Log[]=LR"(D:\DLSSNR-Lab\logs\preblock-live-parameters.txt)";
void* get_hook(void*self,const char*name,uint32_t x,uint32_t y,uint32_t z,uint64_t shared){
 void*result=original_get(self,name,x,y,z,shared);
 if(result&&name){AcquireSRWLockExclusive(&names_lock);names.emplace_back(result,name);ReleaseSRWLockExclusive(&names_lock);}return result;
}
int64_t hook(void*self,void*context,uint32_t x,uint32_t y,uint32_t z,void*blob,uint64_t bytes,uint8_t flag){
 unsigned seq=launches.fetch_add(1);
 if(seq<200){std::string name="unknown";AcquireSRWLockShared(&names_lock);for(const auto&item:names)if(item.first==context){name=item.second;break;}ReleaseSRWLockShared(&names_lock);
  if(FILE*f=_wfopen(Log,L"ab")){fprintf(f,"launch=%u kernel=%s grid=%u,%u,%u bytes=%llu\n",seq,name.c_str(),x,y,z,bytes);fclose(f);}}
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
 auto*getter=reinterpret_cast<unsigned char*>(module)+0x44830;
 const unsigned char get_expected[]={0x40,0x53,0x57,0x48,0x81,0xec,0x58,1,0,0};
 if(memcmp(getter,get_expected,sizeof(get_expected)))return 4;
 const auto init=MH_Initialize();const auto create=MH_CreateHook(target,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original));
 const auto create_get=MH_CreateHook(getter,reinterpret_cast<void*>(&get_hook),reinterpret_cast<void**>(&original_get));
 if(FILE*f=_wfopen(Log,L"wb")){fprintf(f,"format=indirect-v3 init=%d create=%d create_get=%d pid=%lu\n",int(init),int(create),int(create_get),GetCurrentProcessId());fclose(f);}
 if(create!=MH_OK||create_get!=MH_OK)return 3;
 if(MH_EnableHook(getter)!=MH_OK)return 5;
 return MH_EnableHook(target)==MH_OK?0:6;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned);HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
