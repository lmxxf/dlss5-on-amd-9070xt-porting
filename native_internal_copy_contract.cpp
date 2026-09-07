#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
// Version-specific backend observer. No public command-list/NGX entry hooks,
// no resource dereference, and no GPU submissions by this DLL.
namespace {
using Copy=int(__cdecl*)(void*,void*,void*,void*);
Copy original{};std::atomic<unsigned>calls{0};SRWLOCK lock=SRWLOCK_INIT;
#ifndef INTERNAL_COPY_LOG
#define INTERNAL_COPY_LOG LR"(D:\DLSSNR-Lab\logs\native-internal-copy-contract.txt)"
#endif
int __cdecl copy(void*context,void*list,void*dest,void*source){
 unsigned n=++calls;const int result=original(context,list,dest,source);
 if(n<=256){AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(INTERNAL_COPY_LOG,L"ab")){fprintf(f,"copy pid=%lu thread=%lu tick=%llu call=%u context=%p list=%p dest=%p source=%p status=%d metadata_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),n,context,list,dest,source,result);fclose(f);}
  ReleaseSRWLockExclusive(&lock);
 }return result;
}
DWORD WINAPI worker(void*){
 HMODULE module=nullptr;for(unsigned i=0;i<600&&!module;i++){module=GetModuleHandleW(L"nvngx_dlssnr.dll");if(!module)Sleep(100);}
 if(!module)return 1;
 unsigned char expected[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x49,0x8b,0xf9,0x49,0x8b,0xf0,0x48,0x8b,0xda,0x48,0x8b,0xe9};
 auto*target=reinterpret_cast<unsigned char*>(module)+0x5c070;
 unsigned char actual[sizeof(expected)]{};SIZE_T got=0;
 void*table_target=nullptr;SIZE_T table_bytes=0;
 const bool table_match=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<unsigned char*>(module)+0xb6af8,&table_target,sizeof(table_target),&table_bytes)&&table_bytes==sizeof(table_target)&&table_target==target;
 const bool match=table_match&&ReadProcessMemory(GetCurrentProcess(),target,actual,sizeof(actual),&got)&&got==sizeof(actual)&&!memcmp(actual,expected,sizeof(actual));
 if(!match){if(FILE*f=_wfopen(INTERNAL_COPY_LOG,L"ab")){fprintf(f,"pid=%lu signature_mismatch=1 no_hook=1\n",GetCurrentProcessId());fclose(f);}return 2;}
 auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return 3;
 status=MH_CreateHook(target,reinterpret_cast<void*>(&copy),reinterpret_cast<void**>(&original));if(status==MH_OK)status=MH_EnableHook(target);
 if(FILE*f=_wfopen(INTERNAL_COPY_LOG,L"ab")){fprintf(f,"pid=%lu hook_status=%u rva=5c070 signature_match=1\n",GetCurrentProcessId(),unsigned(status));fclose(f);}
 return status==MH_OK?0:4;
}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
  HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!t)return FALSE;CloseHandle(t);
 }return TRUE;
}
