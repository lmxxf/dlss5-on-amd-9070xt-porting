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
struct PendingSnapshot {ID3D12GraphicsCommandList*list{};ID3D12Resource*source{};DWORD thread{};unsigned frame{};ID3D12Resource*motion{};bool reset{};};
// Temporal contract observed on the first FFX dispatch (motion texture size, render size); zero until seen.
static std::atomic<unsigned>observed_motion_w{0},observed_motion_h{0},observed_render_w{0},observed_render_h{0};
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
// Flicker triage: how many FFX dispatches were armed for the neural path and how many actually ran it.
static std::atomic<unsigned> armed_frames{},neural_jobs{},dropped_pending{};
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
 ResourcePayload output{};ID3D12Resource*frame_motion=nullptr;bool frame_reset=false;
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
  // Temporal contract: motion vectors payload (+120), scale/sizes/reset (+360..) per the FFX upscale layout.
  ResourcePayload motion{};float mvscale[4]{};unsigned sizes[4]{};unsigned char reset=0;SIZE_T a=0,b=0,c=0,d=0;
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+120,&motion,sizeof(motion),&a);
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+360,mvscale,16,&b);
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+376,sizes,16,&c);
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+408,&reset,1,&d);
  if(a==sizeof(motion)&&c==16&&motion.resource&&!observed_motion_w.load()){observed_motion_w=motion.width;observed_motion_h=motion.height;observed_render_w=sizes[0];observed_render_h=sizes[1];}
  frame_motion=(a==sizeof(motion)&&motion.width==observed_motion_w.load()&&motion.height==observed_motion_h.load()&&sizes[0]==observed_render_w.load()&&sizes[1]==observed_render_h.load())?static_cast<ID3D12Resource*>(motion.resource):nullptr;
  frame_reset=reset!=0;
  if(n<=8){
   AcquireSRWLockExclusive(&lock);
   if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){
    fprintf(f,"pid=%lu kind=ffx_temporal frame=%u motion=%p format=%u size=%ux%u state=%u jitter=%g,%g mvscale=%g,%g render=%ux%u upscale=%ux%u reset=%u\n",GetCurrentProcessId(),n,motion.resource,motion.format,motion.width,motion.height,motion.state,mvscale[0],mvscale[1],mvscale[2],mvscale[3],sizes[0],sizes[1],sizes[2],sizes[3],unsigned(reset));fclose(f);
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
   if(eligible&&!pending_snapshot.list){auto*r=static_cast<ID3D12Resource*>(output.resource);r->AddRef();if(frame_motion)frame_motion->AddRef();pending_snapshot={native,r,GetCurrentThreadId(),n,frame_motion,frame_reset};++armed_frames;}
   else native->Release();
  }
 }
 if(n%100==0){AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu kind=neural_coverage ffx_frames=%u armed=%u ran=%u dropped_pending=%u\n",GetCurrentProcessId(),n,armed_frames.load(),neural_jobs.load(),dropped_pending.load());fclose(f);}
  ReleaseSRWLockExclusive(&lock);}
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
    pending_snapshot.list->Release();pending_snapshot.source->Release();if(pending_snapshot.motion)pending_snapshot.motion->Release();pending_snapshot={};++dropped_pending;
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
   ++neural_jobs;neural_oneshot.OnSubmitted(q,job.source,job.motion,job.reset,observed_motion_w.load(),observed_motion_h.load(),observed_render_w.load(),observed_render_h.load());if(job.motion)job.motion->Release();
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
// Before the game creates its D3D12 device: select the private Agility 721 runtime shipped in
// the game folder and enable the experimental shader-model feature so SM6.10 wave-matrix PSOs
// can be created on the game device. Gated by D:\DLSSNR-Lab\enable-game-sdk721.txt.
static bool on_create_device(reshade::api::device_api api,uint32_t&){
 if(api!=reshade::api::device_api::d3d12||GetFileAttributesW(LR"(D:\DLSSNR-Lab\enable-game-sdk721.txt)")==INVALID_FILE_ATTRIBUTES)return false;
 static std::atomic<bool>attempted{false};if(attempted.exchange(true))return false;
 const GUID clsid={0x7cda6aca,0xa03e,0x49c8,{0x94,0x58,0x03,0x34,0xd2,0x0e,0x07,0xce}};
 using GetInterfaceFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);HMODULE d3d=GetModuleHandleW(L"d3d12.dll");auto get_interface=d3d?reinterpret_cast<GetInterfaceFn>(GetProcAddress(d3d,"D3D12GetInterface")):nullptr;
 ID3D12SDKConfiguration*configuration=nullptr;HRESULT get=get_interface?get_interface(clsid,IID_PPV_ARGS(&configuration)):HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND),set=E_ABORT,experimental=E_ABORT;
 if(SUCCEEDED(get)){set=configuration->SetSDKVersion(721,".\\DLSS5-D3D12-721\\");configuration->Release();}
 if(SUCCEEDED(set)){const GUID feature={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};experimental=D3D12EnableExperimentalFeatures(1,&feature,nullptr,nullptr);}
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu sdk721_before_device get=%08x set=%08x experimental=%08x\n",GetCurrentProcessId(),unsigned(get),unsigned(set),unsigned(experimental));fclose(f);}
 return false;
}
// VRAM reservation (DLSS5_RESERVE_VRAM_MB=N in native-game-flags.txt): right after the game creates its device, hold N MB of
// video memory in a placeholder buffer so the game sizes its texture pool with that much less; the placeholder is released
// just before the network allocates its own buffers, which then take that room instead of pushing the game's textures out.
static ID3D12Resource*reserved_vram=nullptr;
void NativeReleaseReservedVram(){if(reserved_vram){reserved_vram->Release();reserved_vram=nullptr;if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu reserved_vram_released\n",GetCurrentProcessId());fclose(f);}}}
static void on_init_device(reshade::api::device*device){
 if(!device||device->get_api()!=reshade::api::device_api::d3d12||reserved_vram)return;
 unsigned long mb=0;if(FILE*flags=_wfopen(LR"(D:\DLSSNR-Lab\native-game-flags.txt)",L"rb")){char line[256];while(fgets(line,sizeof line,flags))if(!strncmp(line,"DLSS5_RESERVE_VRAM_MB=",22))mb=strtoul(line+22,nullptr,10);fclose(flags);}
 if(!mb)return;auto*d=reinterpret_cast<ID3D12Device*>(device->get_native());
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(mb)<<20;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 HRESULT hr=d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&reserved_vram));
 if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-submission-order.txt)",L"ab")){fprintf(f,"pid=%lu reserved_vram_mb=%lu hr=%08x\n",GetCurrentProcessId(),mb,unsigned(hr));fclose(f);}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  if(!wcsstr(path,L"SB-Win64-Shipping.exe")||!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::create_device>(on_create_device);
  reshade::register_event<reshade::addon_event::init_device>(on_init_device);
  reshade::register_event<reshade::addon_event::close_command_list>(close_list);
  reshade::register_event<reshade::addon_event::execute_command_list>(execute);
  reshade::register_event<reshade::addon_event::dispatch>(compute);
  reshade::register_event<reshade::addon_event::draw>(draw);
  reshade::register_event<reshade::addon_event::barrier>(barrier);
  HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
  HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
 }return TRUE;
}
