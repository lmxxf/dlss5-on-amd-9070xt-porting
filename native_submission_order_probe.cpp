#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <cstdio>
#include "reshade.hpp"
#include "MinHook.h"
#ifdef NATIVE_ORDER_NEURAL
#define NATIVE_ORDER_SNAPSHOT
#include "native_game_oneshot.h"
static NativeGameOneShot neural_oneshot;
#endif
#ifdef NATIVE_ORDER_SNAPSHOT
#include "native_submitted_readback.h"
#include "native_snapshot_gate.h"
struct PendingSnapshot {ID3D12GraphicsCommandList*list{};ID3D12Resource*source{};DWORD thread{};unsigned frame{};};
static PendingSnapshot pending_snapshot;
static std::mutex snapshot_mutex;
static bool snapshot_taken{};
static thread_local bool snapshot_active{};
#endif
#ifdef NATIVE_ORDER_NEURAL
extern "C" __declspec(dllexport) const char*NAME="DLSS5 AMD single-frame verification";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Background initialization then one reset-history neural frame; not a completed temporal renderer.";
#else
extern "C" __declspec(dllexport) const char*NAME="Native FFX submission order observer";
extern "C" __declspec(dllexport) const char*DESCRIPTION="Records FFX and command-list ordering; optional diagnostic snapshot build.";
#endif
struct Header {uint64_t type;Header*next;};
struct ResourcePayload {void*resource;uint32_t type,format,width,height,depth,mips,flags,usage,state,padding;};
static_assert(sizeof(ResourcePayload)==48,"FFX x64 resource layout");
static std::atomic<uint64_t>tracked_output{};
using Dispatch=uint32_t(*)(void**,const Header*);
static Dispatch original{};
static std::atomic<unsigned> frames{},events{};
static SRWLOCK lock=SRWLOCK_INIT;
using ExecuteLists=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
static ExecuteLists original_execute{};
static std::atomic<bool>execute_install_attempted{};
static std::atomic<unsigned>native_batches{};
static constexpr GUID UnwrappedObject={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
using Barriers=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
static Barriers original_barriers{};
static std::atomic<bool>barrier_install_attempted{};
static void STDMETHODCALLTYPE native_barriers(ID3D12GraphicsCommandList*c,UINT count,const D3D12_RESOURCE_BARRIER*b){
 original_barriers(c,count,b);
 if(!b)return;auto target=tracked_output.load();if(!target)return;
 for(UINT i=0;i<count;i++){
  const auto&v=b[i];
  bool relevant=v.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION?reinterpret_cast<uint64_t>(v.Transition.pResource)==target:
   v.Type==D3D12_RESOURCE_BARRIER_TYPE_UAV?(!v.UAV.pResource||reinterpret_cast<uint64_t>(v.UAV.pResource)==target):
   v.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING?(reinterpret_cast<uint64_t>(v.Aliasing.pResourceBefore)==target||reinterpret_cast<uint64_t>(v.Aliasing.pResourceAfter)==target):false;
  if(!relevant||events.fetch_add(1)>=8192)continue;
  AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
   if(v.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
    fprintf(f,"pid=%lu thread=%lu tick=%llu kind=native_output_barrier list=%p resource=%llx before=%u after=%u subresource=%u flags=%u\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),c,(unsigned long long)target,unsigned(v.Transition.StateBefore),unsigned(v.Transition.StateAfter),v.Transition.Subresource,unsigned(v.Flags));
   else if(v.Type==D3D12_RESOURCE_BARRIER_TYPE_UAV)
    fprintf(f,"pid=%lu thread=%lu tick=%llu kind=native_output_uav list=%p resource=%p global=%u no_state_transition=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),c,v.UAV.pResource,v.UAV.pResource?0u:1u);
   else fprintf(f,"pid=%lu thread=%lu tick=%llu kind=native_output_alias list=%p before_resource=%p after_resource=%p\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),c,v.Aliasing.pResourceBefore,v.Aliasing.pResourceAfter);
   fclose(f);
  }ReleaseSRWLockExclusive(&lock);
 }
}
static void install_native_barriers(void*list){
 if(!list||barrier_install_attempted.exchange(true))return;
 ID3D12GraphicsCommandList*native=nullptr;
 HRESULT hr=static_cast<IUnknown*>(list)->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&native));
 if(FAILED(hr)||!native)return;
 void**table=nullptr;void*target=nullptr;SIZE_T got=0;
 bool readable=ReadProcessMemory(GetCurrentProcess(),native,&table,sizeof(table),&got)&&got==sizeof(table)&&table&&ReadProcessMemory(GetCurrentProcess(),table+26,&target,sizeof(target),&got)&&got==sizeof(target)&&target;
 MH_STATUS s=MH_ERROR_NOT_EXECUTABLE;
 if(readable){s=MH_CreateHook(target,reinterpret_cast<void*>(&native_barriers),reinterpret_cast<void**>(&original_barriers));if(s==MH_OK)s=MH_EnableHook(target);}
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu kind=native_barrier_hook status=%u native_list=%p\n",GetCurrentProcessId(),unsigned(s),native);fclose(f);}native->Release();
}
static void device_identity(const char*origin,ID3D12Device*d){
 if(!d)return;
 IUnknown*identity=nullptr,*unwrapped=nullptr,*native_identity=nullptr;
 HRESULT identity_hr=d->QueryInterface(IID_PPV_ARGS(&identity));
 HRESULT unwrap_hr=d->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&unwrapped));
 HRESULT native_hr=unwrapped?unwrapped->QueryInterface(IID_PPV_ARGS(&native_identity)):E_NOINTERFACE;
 AcquireSRWLockExclusive(&lock);
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
  fprintf(f,"pid=%lu kind=device_identity origin=%s device=%p identity=%p identity_hr=%08x unwrapped=%p unwrap_hr=%08x native_identity=%p native_hr=%08x\n",GetCurrentProcessId(),origin,d,identity,unsigned(identity_hr),unwrapped,unsigned(unwrap_hr),native_identity,unsigned(native_hr));fclose(f);
 }ReleaseSRWLockExclusive(&lock);
 if(native_identity)native_identity->Release();if(unwrapped)unwrapped->Release();if(identity)identity->Release();
}
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
 ResourcePayload output{};
 tracked_output.store(0); // Never attribute later barriers to an unreadable frame.
 SIZE_T output_bytes=0;
 if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+312,&output,sizeof(output),&output_bytes)&&output_bytes==sizeof(output)){
  tracked_output.store(reinterpret_cast<uint64_t>(output.resource));
  if(n<=8&&output.resource){
   auto*r=static_cast<ID3D12Resource*>(output.resource);auto desc=r->GetDesc();ID3D12Device*device=nullptr;auto hr=r->GetDevice(IID_PPV_ARGS(&device));
   device_identity("output",device);
   AcquireSRWLockExclusive(&lock);
   if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
    fprintf(f,"pid=%lu kind=native_output_desc frame=%u resource=%p dimension=%u dxgi=%u size=%llux%u samples=%u mips=%u flags=%u device=%p device_hr=%08x\n",GetCurrentProcessId(),n,r,unsigned(desc.Dimension),unsigned(desc.Format),(unsigned long long)desc.Width,desc.Height,desc.SampleDesc.Count,desc.MipLevels,unsigned(desc.Flags),device,unsigned(hr));fclose(f);
   }ReleaseSRWLockExclusive(&lock);if(device)device->Release();
  }
  if(n<=8){AcquireSRWLockExclusive(&lock);
   if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
    fprintf(f,"pid=%lu thread=%lu tick=%llu kind=ffx_output frame=%u list=%p resource=%p format=%u size=%ux%u declared_state=%u payload_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),n,list,output.resource,output.format,output.width,output.height,output.state);fclose(f);
   }ReleaseSRWLockExclusive(&lock);
  }
 }
 if(output.resource)install_native_barriers(list);
 auto result=original(context,h);log("ffx_end",list,nullptr,result);
#ifdef NATIVE_ORDER_SNAPSHOT
 bool request=n==120;
#ifdef NATIVE_ORDER_NEURAL
 request=request||neural_oneshot.WantsFrame();
#endif
 if(request&&result==0&&output_bytes==sizeof(output)&&output.resource&&output.width==1920&&output.height==1080&&output.state==2&&list){
  ID3D12GraphicsCommandList*native=nullptr;
  if(SUCCEEDED(static_cast<IUnknown*>(list)->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&native)))&&native){
   std::lock_guard<std::mutex>guard(snapshot_mutex);
   bool eligible=!snapshot_taken;
#ifdef NATIVE_ORDER_NEURAL
   eligible=eligible||neural_oneshot.WantsFrame();
#endif
   if(eligible&&!pending_snapshot.list){auto*r=static_cast<ID3D12Resource*>(output.resource);r->AddRef();pending_snapshot={native,r,GetCurrentThreadId(),n};}
   else native->Release();
  }
 }
#endif
 return result;
}
static void STDMETHODCALLTYPE execute_native(ID3D12CommandQueue*q,UINT count,ID3D12CommandList*const*lists){
 const unsigned batch=++native_batches;
 if(q&&batch<=8){
  auto desc=q->GetDesc();ID3D12Device*device=nullptr;auto hr=q->GetDevice(IID_PPV_ARGS(&device));
  device_identity("queue",device);
  AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
   fprintf(f,"pid=%lu kind=native_queue_desc batch=%u queue=%p type=%u flags=%u device=%p device_hr=%08x\n",GetCurrentProcessId(),batch,q,unsigned(desc.Type),unsigned(desc.Flags),device,unsigned(hr));fclose(f);
  }ReleaseSRWLockExclusive(&lock);if(device)device->Release();
 }
 log("execute_native_begin",nullptr,q,count);
 if(lists&&count<=64)for(UINT i=0;i<count;i++)log("execute_native_item",lists[i],q,i);
 original_execute(q,count,lists);
 // This proves CPU submission returned, NOT GPU completion. No fence is added.
 log("execute_native_return",nullptr,q,count);
#ifdef NATIVE_ORDER_SNAPSHOT
 if(!snapshot_active){
  PendingSnapshot job{};
  {std::lock_guard<std::mutex>guard(snapshot_mutex);
   if(pending_snapshot.list&&pending_snapshot.frame!=frames.load()){
    pending_snapshot.list->Release();pending_snapshot.source->Release();pending_snapshot={};
   }
   uintptr_t items[64]{};if(lists&&count<=64)for(UINT i=0;i<count;i++)items[i]=reinterpret_cast<uintptr_t>(lists[i]);
   bool eligible=!snapshot_taken;
#ifdef NATIVE_ORDER_NEURAL
   eligible=eligible||neural_oneshot.WantsFrame();
#endif
   if(eligible&&NativeSnapshotBatchMatch(pending_snapshot.thread,GetCurrentThreadId(),reinterpret_cast<uintptr_t>(pending_snapshot.list),lists?items:nullptr,count)){
    job=pending_snapshot;pending_snapshot={};snapshot_taken=true;
   }
  }
  if(job.list){
   snapshot_active=true;
#ifdef NATIVE_ORDER_NEURAL
   neural_oneshot.OnSubmitted(q,job.source);
#else
   try{
    auto pixels=NativeReadSubmittedFrame(q,job.source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\logs\ffx-submitted-%lu.f16)",GetCurrentProcessId());
    FILE*f=_wfopen(path,L"wb");if(!f)throw std::runtime_error("snapshot file open");auto written=fwrite(pixels.data(),1,pixels.size(),f);fclose(f);if(written!=pixels.size())throw std::runtime_error("snapshot short write");
    if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-snapshot-result.txt)",L"ab")){fprintf(f,"pid=%lu frame=120 bytes=%zu queue=%p source=%p state_restored=UAV success=1\n",GetCurrentProcessId(),pixels.size(),q,job.source);fclose(f);}
   }catch(const std::exception&e){if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-snapshot-result.txt)",L"ab")){fprintf(f,"pid=%lu success=0 error=%s\n",GetCurrentProcessId(),e.what());fclose(f);}}
#endif
   job.list->Release();job.source->Release();snapshot_active=false;
  }
 }
#endif
}
static void install_execute(reshade::api::command_queue*q){
 if(!frames.load()||execute_install_attempted.exchange(true))return;
 auto*native=reinterpret_cast<ID3D12CommandQueue*>(q->get_native());
 void**table=nullptr;void*target=nullptr;SIZE_T got=0;
 if(!ReadProcessMemory(GetCurrentProcess(),native,&table,sizeof(table),&got)||got!=sizeof(table)||!table||
    !ReadProcessMemory(GetCurrentProcess(),table+10,&target,sizeof(target),&got)||got!=sizeof(target)||!target){log("execute_hook_unreadable",native,nullptr);return;}
 // ID3D12CommandQueue's SDK vtable slot10 is ExecuteCommandLists.
 auto s=MH_CreateHook(target,reinterpret_cast<void*>(&execute_native),reinterpret_cast<void**>(&original_execute));
 if(s==MH_OK)s=MH_EnableHook(target);
 log("execute_hook_status",target,native,unsigned(s));
}
static void close_list(reshade::api::command_list*c){log("close_api",c,nullptr);log("close_native",reinterpret_cast<void*>(c->get_native()),nullptr);}
static void execute(reshade::api::command_queue*q,reshade::api::command_list*c){install_execute(q);log("before_execute_api",c,q);log("before_execute_native",reinterpret_cast<void*>(c->get_native()),reinterpret_cast<void*>(q->get_native()));}
static bool compute(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t){log("dispatch_api",c,nullptr);return false;}
static bool draw(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t,uint32_t){log("draw_api",c,nullptr);return false;}
static void barrier(reshade::api::command_list*c,uint32_t count,const reshade::api::resource*r,const reshade::api::resource_usage*before,const reshade::api::resource_usage*after){
 if(!r||!before||!after)return;auto target=tracked_output.load();if(!target)return;
 for(uint32_t i=0;i<count;i++)if(r[i].handle==target&&events.fetch_add(1)<8192){
  AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
   fprintf(f,"pid=%lu thread=%lu tick=%llu kind=output_barrier list=%p resource=%llx before=%u after=%u observation_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),c,(unsigned long long)target,unsigned(before[i]),unsigned(after[i]));fclose(f);
  }ReleaseSRWLockExclusive(&lock);
 }
}
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
  reshade::register_event<reshade::addon_event::barrier>(barrier);
  HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
  HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
 }return TRUE;
}
