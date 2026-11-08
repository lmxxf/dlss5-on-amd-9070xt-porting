#include "native_text_overlay.h"
#include "native_input_geometry.h"
static bool fit_small_input();
#include <mutex>
#include <string>
#include "native_game_submission.h"
#include "native_lab_paths.h"
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
struct PendingSnapshot {ID3D12GraphicsCommandList*list{};ID3D12Resource*source{};DWORD thread{};unsigned frame{};ID3D12Resource*motion{};bool reset{};D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};};
// Temporal contract observed on the first FFX dispatch (motion texture size, render size); zero until seen.
static std::atomic<unsigned>observed_motion_w{0},observed_motion_h{0},observed_render_w{0},observed_render_h{0};
static PendingSnapshot pending_snapshot;
static std::mutex snapshot_mutex;
static bool snapshot_taken{};
static thread_local bool snapshot_active{};
static bool cross_thread_submit{}; /* set for the XeSS path (Rise of the Ronin submits from a different thread than the one recording the upscaler) */
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
#ifdef NATIVE_ORDER_NEURAL
/* on-screen notice (native_text_overlay.h): the upscaler hook decides the text per frame; the notice is drawn from the ExecuteCommandLists hook
   on our own command list right after the game's batch (never recorded into the game's list: doing that crashed D3D12Core in Magpie). */
static NativeTextOverlay text_overlay;static std::mutex notice_mutex;static std::string notice_text;static ID3D12Resource*notice_target=nullptr;static unsigned notice_frame=0;
/* FSR 3.1 ffx_api resource state bits (FfxApiResourceState) -> D3D12 state; false = a combination we do not know */
static bool ffx_state_to_d3d12(uint32_t s,D3D12_RESOURCE_STATES&out){
 switch(s){
  case 1:out=D3D12_RESOURCE_STATE_COMMON;return true;case 2:out=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;return true;
  case 4:out=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;return true;case 8:out=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;return true;
  case 12:out=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;return true;
  case 16:out=D3D12_RESOURCE_STATE_COPY_SOURCE;return true;case 32:out=D3D12_RESOURCE_STATE_COPY_DEST;return true;
  case 20:out=D3D12_RESOURCE_STATE_GENERIC_READ;return true;case 128:out=D3D12_RESOURCE_STATE_PRESENT;return true;case 256:out=D3D12_RESOURCE_STATE_RENDER_TARGET;return true;
  default:return false;
 }
}
#include "native_pre_upscale.h"
static D3D12_RESOURCE_STATES notice_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
static void draw_pending_notice(ID3D12CommandQueue*q){
 std::string text;ID3D12Resource*target=nullptr;
 {std::lock_guard<std::mutex>g(notice_mutex);if(notice_frame&&notice_frame==frames.load()&&notice_target){text=notice_text;target=notice_target;target->AddRef();}notice_frame=0;}
 if(!target)return;
 static NativeGameSubmission*submit=nullptr;static ID3D12CommandQueue*submit_queue=nullptr;
 try{
  if(submit_queue!=q){delete submit;submit=nullptr;submit_queue=nullptr;submit=new NativeGameSubmission;submit->Create(q,false);submit_queue=q;}
  const D3D12_RESOURCE_STATES st=notice_state;submit->Submit([&](ID3D12GraphicsCommandList*c){text_overlay.Draw(c,target,text.c_str(),24,96,3,st);});
 }catch(const std::exception&e){static std::atomic<bool>logged{false};if(!logged.exchange(true))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu notice_submit_failed error=%s\n",GetCurrentProcessId(),e.what());fclose(f);}}
 target->Release();
}
#endif
using Barriers=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
static Barriers original_barriers{};
static std::atomic<bool>barrier_install_attempted{};
static void STDMETHODCALLTYPE native_barriers(ID3D12GraphicsCommandList*c,UINT count,const D3D12_RESOURCE_BARRIER*b){
#ifdef NATIVE_ORDER_NEURAL
 if(NativePreUpscale::FixPrivateBarriers(c,count,b,original_barriers))return;
#endif
 original_barriers(c,count,b);
#ifdef NATIVE_ORDER_NEURAL
 if(NativePreUpscale::HasPendingJobs()&&!NativePreUpscale::Replaying())NativePreUpscale::ObserveBarrier(c,count,b);
#endif
 if(!b)return;auto target=tracked_output.load();if(!target)return;
 for(UINT i=0;i<count;i++){
  const auto&v=b[i];
#ifdef NATIVE_ORDER_SNAPSHOT
  if(fit_small_input()&&!snapshot_active&&v.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION&&v.Flags!=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY){
   std::lock_guard<std::mutex>g(snapshot_mutex);
   if(pending_snapshot.list==c&&pending_snapshot.source==v.Transition.pResource&&v.Transition.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    pending_snapshot.state=v.Transition.StateAfter;
  }
#endif
#ifdef NATIVE_ORDER_NEURAL
  if(fit_small_input()&&!snapshot_active&&v.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION&&v.Flags!=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY){
   std::lock_guard<std::mutex>g(notice_mutex);
   if(notice_frame&&notice_target==v.Transition.pResource&&v.Transition.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)notice_state=v.Transition.StateAfter;
  }
#endif
  bool relevant=v.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION?reinterpret_cast<uint64_t>(v.Transition.pResource)==target:
   v.Type==D3D12_RESOURCE_BARRIER_TYPE_UAV?(!v.UAV.pResource||reinterpret_cast<uint64_t>(v.UAV.pResource)==target):
   v.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING?(reinterpret_cast<uint64_t>(v.Aliasing.pResourceBefore)==target||reinterpret_cast<uint64_t>(v.Aliasing.pResourceAfter)==target):false;
  if(!relevant||events.fetch_add(1)>=8192)continue;
  AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
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
 if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=native_barrier_hook status=%u native_list=%p\n",GetCurrentProcessId(),unsigned(s),native);fclose(f);}native->Release();
}
static void device_identity(const char*origin,ID3D12Device*d){
 if(!d)return;
 IUnknown*identity=nullptr,*unwrapped=nullptr,*native_identity=nullptr;
 HRESULT identity_hr=d->QueryInterface(IID_PPV_ARGS(&identity));
 HRESULT unwrap_hr=d->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&unwrapped));
 HRESULT native_hr=unwrapped?unwrapped->QueryInterface(IID_PPV_ARGS(&native_identity)):E_NOINTERFACE;
 AcquireSRWLockExclusive(&lock);
 if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
  fprintf(f,"pid=%lu kind=device_identity origin=%s device=%p identity=%p identity_hr=%08x unwrapped=%p unwrap_hr=%08x native_identity=%p native_hr=%08x\n",GetCurrentProcessId(),origin,d,identity,unsigned(identity_hr),unwrapped,unsigned(unwrap_hr),native_identity,unsigned(native_hr));fclose(f);
 }ReleaseSRWLockExclusive(&lock);
 if(native_identity)native_identity->Release();if(unwrapped)unwrapped->Release();if(identity)identity->Release();
}
static void log(const char*kind,void*list,void*queue,unsigned value=0){
 if(!frames.load()||events.load(std::memory_order_relaxed)>=8192||events.fetch_add(1)>=8192)return;
 AcquireSRWLockExclusive(&lock);
 if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
  fprintf(f,"pid=%lu thread=%lu tick=%llu kind=%s list=%p queue=%p value=%u observation_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),kind,list,queue,value);fclose(f);
 }ReleaseSRWLockExclusive(&lock);
}
/* Read before initialization: the usual environment flags are applied by the background loader. */
static bool fit_small_input(){static const bool enabled=[]{unsigned v=0;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();sscanf(line,"DLSS5_FIT_INPUT=%u",&v);}return v==1;}();return enabled;}
static bool fit_large_input(){static const bool enabled=[]{unsigned v=0;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();sscanf(line,"DLSS5_FIT_LARGE=%u",&v);}if(v==1)NativeFitLargeInputOverride()=true;return v==1;}();return enabled;}
static bool supported_input(unsigned w,unsigned h){return (w==1920&&h==1080)||(fit_small_input()&&NativeInputGeometry::Supported(w,h,fit_large_input()));}
static std::atomic<void**>fit_context{nullptr};
using DestroyContext=uint32_t(*)(void**,const void*);
static DestroyContext original_destroy{};
static DestroyContext original_destroy_shim{};
static std::atomic<unsigned>foreign_dispatches{0};
static uint32_t destroy_context_shim(void**context,const void*allocation_callbacks){
 auto result=original_destroy_shim(context,allocation_callbacks);
 if(result==0){auto expected=context;if(fit_context.compare_exchange_strong(expected,nullptr))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=fit_context_destroyed via=shim context=%p\n",GetCurrentProcessId(),context);fclose(f);}}
 return result;
}
static uint32_t destroy_context(void**context,const void*allocation_callbacks){
 auto result=original_destroy(context,allocation_callbacks);
 if(result==0){auto expected=context;if(fit_context.compare_exchange_strong(expected,nullptr))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=fit_context_destroyed via=provider context=%p\n",GetCurrentProcessId(),context);fclose(f);}}
 return result;
}
/* 2026-09-24: two entry points. OptiScaler 0.9.4 ships the FSR SDK 2.0 split dlls: amd_fidelityfx_dx12.dll (26 KB shim) forwards
   to amd_fidelityfx_upscaler_dx12.dll. Stellar Blade's host dispatches through the shim's export (and the shim reaches the provider
   without going through the provider's export symbol), Cyberpunk 2077's host calls the provider export directly. Hook both; a call that
   entered through the shim is passed straight through at the provider layer (thread-local depth). The layer that observed the call
   supplies a per-job trampoline for deferred replay. Provider contexts and shim wrappers are not interchangeable. */
static Dispatch original_shim{},original_sr{};static thread_local unsigned shim_depth=0;
static uint32_t dispatch_core(void**context,const Header*h,Dispatch orig);
static uint32_t dispatch_shim(void**context,const Header*h){++shim_depth;uint32_t r=dispatch_core(context,h,original_shim);--shim_depth;return r;}
static uint32_t dispatch_sr(void**context,const Header*h){if(shim_depth)return original_sr(context,h);return dispatch_core(context,h,original_sr);}
static uint32_t dispatch_core(void**context,const Header*h,Dispatch orig){

 {static std::atomic<unsigned>seen{};unsigned k=seen.fetch_add(1);if(k<8)if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=ffx_dispatch_entry n=%u type=%08x context=%p\n",GetCurrentProcessId(),k,h?unsigned(h->type):0u,context);fclose(f);}}
 if(!h||(h->type&0x00ffffffu)!=0x00010001u)return orig(context,h);
 /* Select the first upscaler context until it is destroyed (including its size-error notice). A following FSR4 (even when its output
    also fits 1080p) must not replace the input stage's motion, pending work or status text. */
 if(fit_small_input()){
  auto chosen=fit_context.load();
  if(chosen&&chosen!=context){
   /* 2026-09-25: Stellar Blade preset switch -> OptiScaler creates a new FSR context and releases the old one; when the release
      does not reach our destroy hook the pin stuck to a dead context and every later dispatch passed through in silence. A pinned
      context that has not dispatched for 120 foreign dispatches is treated as gone (a second live context alternating with the
      chosen one resets the count on every chosen dispatch, so the FSR4-follower case keeps its pin). */
   if(foreign_dispatches.fetch_add(1)+1>=120){fit_context.compare_exchange_strong(chosen,nullptr);foreign_dispatches.store(0);
    if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=fit_context_reassigned old=%p new=%p\n",GetCurrentProcessId(),chosen,context);fclose(f);}}
   else return orig(context,h);
  }else foreign_dispatches.store(0);
  ResourcePayload candidate{};SIZE_T bytes=0;
  if(context&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+312,&candidate,sizeof candidate,&bytes)&&bytes==sizeof candidate&&candidate.resource){
   void**expected=nullptr;fit_context.compare_exchange_strong(expected,context);
   if(fit_context.load()!=context)return orig(context,h);
  }
 }
 // Existing FFX x64 ABI: commandList follows the16-byte header. Payload only.
 void*list=nullptr;SIZE_T got=0;
 ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+16,&list,sizeof(list),&got);
 unsigned n=++frames;log("ffx_begin",got==sizeof(list)?list:nullptr,nullptr,n);
#ifdef NATIVE_ORDER_NEURAL
 if(NativePreUpscale::Enabled()){
  if(NativePreUpscale::Replaying())return orig(context,h);
  if(NativePreUpscale::Mode()==1&&neural_oneshot.Bypassed()){
   if(n<5||n%100==0)log("ffx_passthrough_f6",list,nullptr,n);
   return orig(context,h);
  }
  ID3D12GraphicsCommandList*native=nullptr;
  if(list&&SUCCEEDED(static_cast<IUnknown*>(list)->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&native)))&&native){
   install_native_barriers(list);
   const bool captured=NativePreUpscale::Capture(context,h,native,n,orig);native->Release();
   if(captured)return 0;
  }
  /* Unsupported descriptors run the original FSR unchanged, never the old post-upscale network. */
  return orig(context,h);
 }
#endif
 ResourcePayload output{};ID3D12Resource*frame_motion=nullptr;bool frame_reset=false;
 tracked_output.store(0); // Never attribute later barriers to an unreadable frame.
 SIZE_T output_bytes=0;
 if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+312,&output,sizeof(output),&output_bytes)&&output_bytes==sizeof(output)){
  tracked_output.store(reinterpret_cast<uint64_t>(output.resource));
  if(n<=8&&output.resource){
   auto*r=static_cast<ID3D12Resource*>(output.resource);auto desc=r->GetDesc();ID3D12Device*device=nullptr;auto hr=r->GetDevice(IID_PPV_ARGS(&device));
   device_identity("output",device);
   AcquireSRWLockExclusive(&lock);
   if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
    fprintf(f,"pid=%lu kind=native_output_desc frame=%u resource=%p dimension=%u dxgi=%u size=%llux%u samples=%u mips=%u flags=%u device=%p device_hr=%08x\n",GetCurrentProcessId(),n,r,unsigned(desc.Dimension),unsigned(desc.Format),(unsigned long long)desc.Width,desc.Height,desc.SampleDesc.Count,desc.MipLevels,unsigned(desc.Flags),device,unsigned(hr));fclose(f);
   }ReleaseSRWLockExclusive(&lock);if(device)device->Release();
  }
  if(n<=8){AcquireSRWLockExclusive(&lock);
   if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
    fprintf(f,"pid=%lu thread=%lu tick=%llu kind=ffx_output frame=%u list=%p resource=%p format=%u size=%ux%u declared_state=%u payload_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),n,list,output.resource,output.format,output.width,output.height,output.state);fclose(f);
   }ReleaseSRWLockExclusive(&lock);
  }
  // Temporal contract: motion vectors payload (+120), scale/sizes/reset (+360..) per the FFX upscale layout.
  ResourcePayload motion{};float mvscale[4]{};unsigned sizes[4]{};unsigned char reset=0;SIZE_T a=0,b=0,c=0,d=0;
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+120,&motion,sizeof(motion),&a);
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+360,mvscale,16,&b);
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+376,sizes,16,&c);
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const char*>(h)+408,&reset,1,&d);
  if(a==sizeof(motion)&&c==16&&motion.resource&&(!observed_motion_w.load()||fit_small_input())){observed_motion_w=motion.width;observed_motion_h=motion.height;observed_render_w=sizes[0];observed_render_h=sizes[1];if(b==16){NativeMotionVectorScale()[0]=mvscale[0];NativeMotionVectorScale()[1]=mvscale[1];}}
  frame_motion=(a==sizeof(motion)&&motion.width==observed_motion_w.load()&&motion.height==observed_motion_h.load()&&sizes[0]==observed_render_w.load()&&sizes[1]==observed_render_h.load())?static_cast<ID3D12Resource*>(motion.resource):nullptr;
  frame_reset=reset!=0;
  if(n<=8){
   AcquireSRWLockExclusive(&lock);
   if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
    fprintf(f,"pid=%lu kind=ffx_temporal frame=%u motion=%p format=%u size=%ux%u state=%u jitter=%g,%g mvscale=%g,%g render=%ux%u upscale=%ux%u reset=%u\n",GetCurrentProcessId(),n,motion.resource,motion.format,motion.width,motion.height,motion.state,mvscale[0],mvscale[1],mvscale[2],mvscale[3],sizes[0],sizes[1],sizes[2],sizes[3],unsigned(reset));fclose(f);
   }ReleaseSRWLockExclusive(&lock);
  }
 }
 if(output.resource)install_native_barriers(list);
 auto result=orig(context,h);log("ffx_end",list,nullptr,result);
#ifdef NATIVE_ORDER_NEURAL
 /* on-screen notice (native_text_overlay.h): why the picture is not changing -- the input is not 1920x1080 (the network only knows that
    size; the hook never arms), the network is still initializing, or its initialization failed (developer mode off, wrong driver...) */
 static const unsigned notice_mode=[]{unsigned v=2;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{unsigned x;if(sscanf(line,"DLSS5_NOTICE=%u",&x)==1)v=x;}}return v;}();
 D3D12_RESOURCE_STATES declared_state{};const bool state_known=output_bytes==sizeof(output)&&ffx_state_to_d3d12(output.state,declared_state);
 if(output_bytes==sizeof(output)&&!state_known){static std::atomic<bool>logged{false};if(!logged.exchange(true))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=output_state_unknown detail=the upscaler declares its output in ffx state %u, which the hook cannot map to a D3D12 state; please report this line\n",GetCurrentProcessId(),GetTickCount64(),output.state);fclose(f);}}
 if(notice_mode&&output_bytes==sizeof(output)&&output.resource&&state_known&&list){
  static std::atomic<bool>size_logged{false};char notice[80]{};
  if(!supported_input(output.width,output.height)){snprintf(notice,sizeof notice,fit_small_input()?"DLSS5-AMD: INPUT MAX 1920X1080 (NOW %uX%u)":"DLSS5-AMD: INPUT MUST BE 1920X1080 (NOW %uX%u)",output.width,output.height);
   if(!size_logged.exchange(true))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=input_size_unsupported detail=upscaler runs at %ux%u, expected first-stage output within 1920x1080 with DLSS5_FIT_INPUT=1, otherwise exactly 1920x1080 (Magpie: FSR3 scale relative to input = 1)\n",GetCurrentProcessId(),GetTickCount64(),output.width,output.height);fclose(f);}}
  else{const unsigned ph=neural_oneshot.Phase();if(ph==0)text_overlay.Prepare(static_cast<ID3D12Resource*>(output.resource)); /* pipeline built before the initializer thread starts (same frame arms it) */
   if(ph==1&&text_overlay.Ready())snprintf(notice,sizeof notice,"DLSS5-AMD: INITIALIZING...");else if(ph==5)snprintf(notice,sizeof notice,"DLSS5-AMD: INIT FAILED - SEE DLSS5-AMD\\LOGS");
  }
  if(notice[0]&&notice_mode>=2){std::lock_guard<std::mutex>g(notice_mutex);notice_text=notice;notice_state=declared_state;auto*r=static_cast<ID3D12Resource*>(output.resource);if(notice_target!=r){if(notice_target)notice_target->Release();notice_target=r;notice_target->AddRef();}notice_frame=n;}
 }
#endif
#ifdef NATIVE_ORDER_SNAPSHOT
 /* DLSS5_SNAPSHOT_FRAME=<n> in native-game-flags.txt (read here directly: the flag file is applied to the environment only when the network
    initializes, which is what this frame triggers): the upscaler frame that arms the network. Default 120 (the game build); 1 for Magpie. */
 static const unsigned snapshot_frame=[]{unsigned v=120;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{unsigned x;if(sscanf(line,"DLSS5_SNAPSHOT_FRAME=%u",&x)==1&&x>=1)v=x;}}return v;}();
 bool request=n==snapshot_frame;
#ifdef NATIVE_ORDER_NEURAL
 /* idle (first time, or after a session reset) and failed states re-arm from the snapshot frame on; the first arming is still exactly snapshot_frame */
 request=neural_oneshot.WantsFrame()||(n>=snapshot_frame&&(neural_oneshot.Phase()==0||neural_oneshot.Phase()==5));
#endif
 /* any declared output state we can map is taken over (the frame transitions from / back to it); 2 = UAV is what Stellar Blade and Magpie declare */
 if(request&&result==0&&output_bytes==sizeof(output)&&output.resource&&supported_input(output.width,output.height)&&state_known&&list){
  ID3D12GraphicsCommandList*native=nullptr;
  if(SUCCEEDED(static_cast<IUnknown*>(list)->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&native)))&&native){
   std::lock_guard<std::mutex>guard(snapshot_mutex);
   bool eligible=!snapshot_taken;
#ifdef NATIVE_ORDER_NEURAL
   eligible=eligible||neural_oneshot.WantsFrame()||neural_oneshot.Phase()==0||neural_oneshot.Phase()==5;
#endif
   if(eligible&&!pending_snapshot.list){auto*r=static_cast<ID3D12Resource*>(output.resource);r->AddRef();if(frame_motion)frame_motion->AddRef();pending_snapshot={native,r,GetCurrentThreadId(),n,frame_motion,frame_reset,declared_state};++armed_frames;}
   else native->Release();
  }
 }
 if(n%100==0){AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=neural_coverage ffx_frames=%u armed=%u ran=%u dropped_pending=%u\n",GetCurrentProcessId(),n,armed_frames.load(),neural_jobs.load(),dropped_pending.load());fclose(f);}
  ReleaseSRWLockExclusive(&lock);}
#endif
 return result;
}
#ifdef NATIVE_ORDER_NEURAL
// XeSS titles (Rise of the Ronin): the same contract read from xessD3D12Execute's parameters (XeSS 1.x/2.x SDK layout)
// instead of the FFX dispatch description. XeSS runs first; the network then refines its 1080p output after submission,
// exactly as with FSR. Velocity is XeSS-scaled by (-w,-h) in this title, so the feed's motion sign is flipped.
struct XessExecuteParams{ID3D12Resource*color,*velocity,*depth,*exposure,*responsive,*output;float jitter_x,jitter_y,exposure_scale;uint32_t reset,input_w,input_h;int32_t bases[10];ID3D12DescriptorHeap*heap;uint32_t heap_offset;};
using XessExecute=int(*)(void*,ID3D12GraphicsCommandList*,const XessExecuteParams*);
static XessExecute original_xess{};
static int xess_execute(void*ctx,ID3D12GraphicsCommandList*list,const XessExecuteParams*p){
 if(!p||!p->output)return original_xess(ctx,list,p);
 unsigned n=++frames;log("xess_begin",list,nullptr,n);
 auto*out=p->output;auto od=out->GetDesc();tracked_output.store(reinterpret_cast<uint64_t>(out));
 D3D12_RESOURCE_DESC md{};if(p->velocity)md=p->velocity->GetDesc();
 if(p->velocity&&!observed_motion_w.load()){observed_motion_w=unsigned(md.Width);observed_motion_h=md.Height;observed_render_w=p->input_w;observed_render_h=p->input_h;}
 ID3D12Resource*frame_motion=(p->velocity&&unsigned(md.Width)==observed_motion_w.load()&&md.Height==observed_motion_h.load()&&p->input_w==observed_render_w.load()&&p->input_h==observed_render_h.load())?p->velocity:nullptr;
 const bool frame_reset=p->reset!=0;
 if(n<=8){AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
   fprintf(f,"pid=%lu kind=xess_execute frame=%u list=%p output=%p format=%u size=%llux%u flags=%u velocity=%p vformat=%u vsize=%llux%u input=%ux%u jitter=%g,%g exposure=%g reset=%u\n",GetCurrentProcessId(),n,list,out,unsigned(od.Format),(unsigned long long)od.Width,od.Height,unsigned(od.Flags),p->velocity,unsigned(md.Format),(unsigned long long)md.Width,md.Height,p->input_w,p->input_h,p->jitter_x,p->jitter_y,p->exposure_scale,p->reset);fclose(f);
  }ReleaseSRWLockExclusive(&lock);}
 install_native_barriers(list);
 int result=original_xess(ctx,list,p);log("xess_end",list,nullptr,unsigned(result));
 /* 2026-09-18: the same contract as the FFX path (it used to be the Rise of the Ronin one: exactly 1920x1080, armed once at frame 120, no
    on-screen notice) -- any supported input size, re-armed from the snapshot frame on while idle/failed, the notice text on the XeSS output. */
 static const unsigned xess_snapshot_frame=[]{unsigned v=120;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{unsigned x;if(sscanf(line,"DLSS5_SNAPSHOT_FRAME=%u",&x)==1&&x>=1)v=x;}}return v;}();
 static const unsigned xess_notice_mode=[]{unsigned v=2;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{unsigned x;if(sscanf(line,"DLSS5_NOTICE=%u",&x)==1)v=x;}}return v;}();
 const bool size_ok=supported_input(unsigned(od.Width),od.Height);
 if(xess_notice_mode&&list){
  static std::atomic<bool>size_logged{false};char notice[80]{};
  if(!size_ok){snprintf(notice,sizeof notice,fit_small_input()?"DLSS5-AMD: INPUT MAX 1920X1080 (NOW %uX%u)":"DLSS5-AMD: INPUT MUST BE 1920X1080 (NOW %uX%u)",unsigned(od.Width),od.Height);
   if(!size_logged.exchange(true))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=input_size_unsupported detail=XeSS output %llux%u, expected within 1920x1080 with DLSS5_FIT_INPUT=1\n",GetCurrentProcessId(),GetTickCount64(),(unsigned long long)od.Width,od.Height);fclose(f);}}
  else{const unsigned ph=neural_oneshot.Phase();if(ph==0)text_overlay.Prepare(out);
   if(ph==1&&text_overlay.Ready())snprintf(notice,sizeof notice,"DLSS5-AMD: INITIALIZING...");else if(ph==5)snprintf(notice,sizeof notice,"DLSS5-AMD: INIT FAILED - SEE DLSS5-AMD\\LOGS");}
  if(notice[0]&&xess_notice_mode>=2){std::lock_guard<std::mutex>g(notice_mutex);notice_text=notice;notice_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;if(notice_target!=out){if(notice_target)notice_target->Release();notice_target=out;notice_target->AddRef();}notice_frame=n;}
 }
 const bool request=neural_oneshot.WantsFrame()||(n>=xess_snapshot_frame&&(neural_oneshot.Phase()==0||neural_oneshot.Phase()==5));
 if(request&&result==0&&size_ok&&list){
  ID3D12GraphicsCommandList*native=nullptr;
  if(SUCCEEDED(static_cast<IUnknown*>(list)->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(&native)))&&native){
   std::lock_guard<std::mutex>guard(snapshot_mutex);
   const bool eligible=!snapshot_taken||neural_oneshot.WantsFrame()||neural_oneshot.Phase()==0||neural_oneshot.Phase()==5;
   if(eligible&&!pending_snapshot.list){out->AddRef();if(frame_motion)frame_motion->AddRef();pending_snapshot={native,out,GetCurrentThreadId(),n,frame_motion,frame_reset,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};++armed_frames;}
   else native->Release();
  }
 }
 if(n%100==0){AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=neural_coverage xess_frames=%u armed=%u ran=%u dropped_pending=%u\n",GetCurrentProcessId(),n,armed_frames.load(),neural_jobs.load(),dropped_pending.load());fclose(f);}
  ReleaseSRWLockExclusive(&lock);}
 return result;
}
#endif
static void STDMETHODCALLTYPE execute_native(ID3D12CommandQueue*q,UINT count,ID3D12CommandList*const*lists){
#ifdef NATIVE_ORDER_NEURAL
 if(NativePreUpscale::Enabled()&&!NativePreUpscale::Replaying()&&NativePreUpscale::Execute(q,count,lists,original_execute))return;
#endif
 const unsigned batch=++native_batches;
 if(q&&batch<=8){
  auto desc=q->GetDesc();ID3D12Device*device=nullptr;auto hr=q->GetDevice(IID_PPV_ARGS(&device));
  device_identity("queue",device);
  AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
   fprintf(f,"pid=%lu kind=native_queue_desc batch=%u queue=%p type=%u flags=%u device=%p device_hr=%08x\n",GetCurrentProcessId(),batch,q,unsigned(desc.Type),unsigned(desc.Flags),device,unsigned(hr));fclose(f);
  }ReleaseSRWLockExclusive(&lock);if(device)device->Release();
 }
 log("execute_native_begin",nullptr,q,count);
 if(lists&&count<=64)for(UINT i=0;i<count;i++)log("execute_native_item",lists[i],q,i);
#if defined(NATIVE_ORDER_SNAPSHOT)&&defined(NATIVE_ORDER_NEURAL)
 /* DLSS5_SPLIT_SUBMIT=1 (native-game-flags.txt; 2026-09-17, Black Myth: Wukong): engines that record the upscaler on one thread and
    submit from another (UE5 RHI thread), with the upscaler's list in the middle of a batch whose later lists already consume its
    output. The armed list is matched by identity anywhere in the batch, on any thread, up to two upscaler calls behind; the batch is
    executed in two halves with the network between them, so the refined frame is what the rest of the batch reads. Off = the
    Stellar Blade contract (same thread, last list of the batch, network after the whole batch). */
 static const bool split_submit=[]{unsigned v=0;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{unsigned x;if(sscanf(line,"DLSS5_SPLIT_SUBMIT=%u",&x)==1)v=x;}}return v!=0;}();
 if(split_submit&&!snapshot_active&&lists&&count&&count<=64){
  PendingSnapshot job{};UINT at=count;
  {std::lock_guard<std::mutex>guard(snapshot_mutex);
   if(pending_snapshot.list&&frames.load()-pending_snapshot.frame>2){pending_snapshot.list->Release();pending_snapshot.source->Release();if(pending_snapshot.motion)pending_snapshot.motion->Release();pending_snapshot={};++dropped_pending;}
   bool eligible=!snapshot_taken||neural_oneshot.WantsFrame()||neural_oneshot.Phase()==0||neural_oneshot.Phase()==5;
   if(eligible&&pending_snapshot.list)for(UINT i=0;i<count;i++)if(reinterpret_cast<ID3D12CommandList*>(pending_snapshot.list)==lists[i]){at=i;break;}
   if(at<count){job=pending_snapshot;pending_snapshot={};snapshot_taken=true;}
  }
  if(job.list){
   static std::atomic<unsigned>split_logged{};if(split_logged.fetch_add(1)<8)if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu kind=split_submit thread=%lu producer_thread=%lu frame=%u at=%u count=%u\n",GetCurrentProcessId(),GetCurrentThreadId(),job.thread,job.frame,unsigned(at),unsigned(count));fclose(f);}
   original_execute(q,at+1,lists);
   log("execute_native_return",nullptr,q,at+1);
   snapshot_active=true;
   ++neural_jobs;neural_oneshot.OnSubmitted(q,job.source,job.motion,job.reset,observed_motion_w.load(),observed_motion_h.load(),observed_render_w.load(),observed_render_h.load(),job.state);if(job.motion)job.motion->Release();
   job.list->Release();job.source->Release();snapshot_active=false;
   if(at+1<count)original_execute(q,count-at-1,lists+at+1);
   draw_pending_notice(q);
   return;
  }
 }
#endif
 original_execute(q,count,lists);
 // This proves CPU submission returned, NOT GPU completion. No fence is added.
 log("execute_native_return",nullptr,q,count);
#ifdef NATIVE_ORDER_NEURAL
 draw_pending_notice(q);
#endif
#ifdef NATIVE_ORDER_SNAPSHOT
 if(!snapshot_active){
  PendingSnapshot job{};
  {std::lock_guard<std::mutex>guard(snapshot_mutex);
   if(pending_snapshot.list&&(cross_thread_submit?frames.load()-pending_snapshot.frame>2:pending_snapshot.frame!=frames.load())){ /* cross-thread: the render thread may be one or two upscaler calls ahead of the submit */
    pending_snapshot.list->Release();pending_snapshot.source->Release();if(pending_snapshot.motion)pending_snapshot.motion->Release();pending_snapshot={};++dropped_pending;
   }
   uintptr_t items[64]{};if(lists&&count<=64)for(UINT i=0;i<count;i++)items[i]=reinterpret_cast<uintptr_t>(lists[i]);
   bool eligible=!snapshot_taken;
#ifdef NATIVE_ORDER_NEURAL
   eligible=eligible||neural_oneshot.WantsFrame()||neural_oneshot.Phase()==0||neural_oneshot.Phase()==5; /* idle/failed: a new snapshot (re)initializes */
#endif
   if(eligible&&NativeSnapshotBatchMatch(cross_thread_submit?GetCurrentThreadId():pending_snapshot.thread,GetCurrentThreadId(),reinterpret_cast<uintptr_t>(pending_snapshot.list),lists?items:nullptr,count)){ /* XeSS titles record on the render thread and submit from another: match by list identity only */
    job=pending_snapshot;pending_snapshot={};snapshot_taken=true;
   }
  }
  if(job.list){
   snapshot_active=true;
#ifdef NATIVE_ORDER_NEURAL
   ++neural_jobs;neural_oneshot.OnSubmitted(q,job.source,job.motion,job.reset,observed_motion_w.load(),observed_motion_h.load(),observed_render_w.load(),observed_render_h.load(),job.state);if(job.motion)job.motion->Release();
#else
   try{
    auto pixels=NativeReadSubmittedFrame(q,job.source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,NativeLabPath(L"logs\\ffx-submitted-%lu.f16").c_str(),GetCurrentProcessId());
    FILE*f=_wfopen(path,L"wb");if(!f)throw std::runtime_error("snapshot file open");auto written=fwrite(pixels.data(),1,pixels.size(),f);fclose(f);if(written!=pixels.size())throw std::runtime_error("snapshot short write");
    if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-snapshot-result.txt").c_str(),L"ab")){fprintf(f,"pid=%lu frame=120 bytes=%zu queue=%p source=%p state_restored=UAV success=1\n",GetCurrentProcessId(),pixels.size(),q,job.source);fclose(f);}
   }catch(const std::exception&e){if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-snapshot-result.txt").c_str(),L"ab")){fprintf(f,"pid=%lu success=0 error=%s\n",GetCurrentProcessId(),e.what());fclose(f);}}
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
/* 2026-09-18: frames presented by the host. The upscaler hook waits for the first 30 of them: MinHook suspends every thread to patch the
   export, and doing that while the title is still loading dlls (Black Myth: Wukong loads libxess.dll at start-up and keeps loading EOS/Steam
   right after) deadlocked the loader on most launches -- the game then sat black for its 60 s watchdog and closed cleanly. */
static std::atomic<unsigned>presents{};
static void on_present(reshade::api::command_queue*,reshade::api::swapchain*,const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*){++presents;}
static void pre_upscale_work(reshade::api::command_list*c){
#ifdef NATIVE_ORDER_NEURAL
 if(NativePreUpscale::HasPendingJobs())NativePreUpscale::ObserveWork(reinterpret_cast<ID3D12GraphicsCommandList*>(c->get_native()));
#endif
}
static bool compute(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t){pre_upscale_work(c);log("dispatch_api",c,nullptr);return false;}
static bool draw(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t,uint32_t){pre_upscale_work(c);log("draw_api",c,nullptr);return false;}
static bool draw_indexed(reshade::api::command_list*c,uint32_t,uint32_t,uint32_t,int32_t,uint32_t){pre_upscale_work(c);return false;}
static void barrier(reshade::api::command_list*c,uint32_t count,const reshade::api::resource*r,const reshade::api::resource_usage*before,const reshade::api::resource_usage*after){
 if(!r||!before||!after)return;auto target=tracked_output.load();if(!target)return;
 for(uint32_t i=0;i<count;i++)if(r[i].handle==target&&events.fetch_add(1)<8192){
  AcquireSRWLockExclusive(&lock);
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){
   fprintf(f,"pid=%lu thread=%lu tick=%llu kind=output_barrier list=%p resource=%llx before=%u after=%u observation_only=1\n",GetCurrentProcessId(),GetCurrentThreadId(),GetTickCount64(),c,(unsigned long long)target,unsigned(before[i]),unsigned(after[i]));fclose(f);
  }ReleaseSRWLockExclusive(&lock);
 }
}
static DWORD WINAPI worker(void*){
#ifdef NATIVE_ORDER_NEURAL
 NativePreUpscale::InstallExceptionTrace();
#endif
 // Whichever upscaler dll the title loads first: the FFX SDK 1.x single dll (Stellar Blade), the FSR 4 SDK loader (Magpie's FSR3/FSR4
 // effects: amd_fidelityfx_loader_dx12.dll exports ffxDispatch and forwards to the provider dll) or XeSS (Rise of the Ronin; its FSR is linked into the exe).
 // A host that ships an FFX dll next to its exe (Magpie: libxess.dll is loaded at startup, the FFX loader only when the FSR3/FSR4 effect
 // starts) waits for the FFX one; XeSS is taken only when no FFX dll file is present (Rise of the Ronin). DLSS5_UPSCALER=ffx|xess overrides.
 bool wait_ffx=false;{wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);if(wchar_t*slash=wcsrchr(exe,L'\\'))slash[1]=0;std::wstring dir=exe;wait_ffx=GetFileAttributesW((dir+L"amd_fidelityfx_dx12.dll").c_str())!=INVALID_FILE_ATTRIBUTES||GetFileAttributesW((dir+L"amd_fidelityfx_loader_dx12.dll").c_str())!=INVALID_FILE_ATTRIBUTES;}
 /* DLSS5_UPSCALER=ffx|xess also from native-game-flags.txt (2026-09-18: Black Myth: Wukong ships FFX dlls but links its FSR3 upscaler
    statically, so the FFX hook never fires there; the XeSS path is the one to take). The environment variable still wins. */
 bool only_xess=false; /* an explicit xess choice ignores the FFX dlls even when they are loaded (Wukong loads them at start-up for frame generation) */
 for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{char v[8]{};if(sscanf(line,"DLSS5_UPSCALER=%7s",v)==1){if(!strcmp(v,"ffx")){wait_ffx=true;only_xess=false;}else if(!strcmp(v,"xess")){wait_ffx=false;only_xess=true;}}}}
 if(const wchar_t*u=_wgetenv(L"DLSS5_UPSCALER")){if(!wcscmp(u,L"ffx")){wait_ffx=true;only_xess=false;}else if(!wcscmp(u,L"xess")){wait_ffx=false;only_xess=true;}}
 /* weights into memory while we wait for the upscaler dll / the user's hotkey (see NativePrefetchWeights) */
 {std::wstring assets=NativeLabPath(L"native-game-tiled-assets");if(GetFileAttributesW(assets.c_str())!=INVALID_FILE_ATTRIBUTES)NativePrefetchWeights(assets);}
 /* No deadline: Magpie loads the FFX dll only when the user starts scaling, which can be any time after launch (the old 10-minute limit gave up before that). */
 /* 2026-09-23 (Wo Long 2 demo): OptiScaler 0.9.4 ships the FSR SDK 2.0 split dlls; amd_fidelityfx_dx12.dll is a 26 KB shim that forwards
    to amd_fidelityfx_upscaler_dx12.dll. Stellar Blade's host goes through the shim, Wo Long 2's host creates the context on the upscaler dll
    directly ("Creating with upscaling_dx12"), so a hook on the shim never fires there. Hook the upscaler dll first when it is loaded: the shim
    forwards into the same export, so one hook covers both routes without double dispatch. */
 HMODULE module=nullptr,xess=nullptr;for(unsigned i=0;!module&&!xess;i++){if(!only_xess){module=GetModuleHandleW(L"amd_fidelityfx_upscaler_dx12.dll");if(!module)module=GetModuleHandleW(L"amd_fidelityfx_dx12.dll");if(!module)module=GetModuleHandleW(L"amd_fidelityfx_loader_dx12.dll");}if(!wait_ffx)xess=GetModuleHandleW(L"libxess.dll");if(!module&&!xess)Sleep(100);}if(!module&&!xess)return 1;
 auto target=module?GetProcAddress(module,"ffxDispatch"):GetProcAddress(xess,"xessD3D12Execute");if(!target)return 2;
 while(presents.load()<30)Sleep(100); /* past start-up dll loading: see on_present */
 auto s=MH_Initialize();if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED)return 3;
 if(module&&fit_small_input()){
  /* Magpie unloads the FFX loader when scaling stops. Hook trampolines must remain executable across restarts. */
  HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(target),&pinned))return 5;
  auto destroy=GetProcAddress(module,"ffxDestroyContext");
  auto ds=destroy?MH_CreateHook(reinterpret_cast<void*>(destroy),reinterpret_cast<void*>(&destroy_context),reinterpret_cast<void**>(&original_destroy)):MH_ERROR_NOT_EXECUTABLE;
  if(ds==MH_OK)ds=MH_EnableHook(reinterpret_cast<void*>(destroy));
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu fit_context_destroy_hook=%u\n",GetCurrentProcessId(),unsigned(ds));fclose(f);}
  if(ds!=MH_OK)return 5;
  /* the shim's own ffxDestroyContext (Stellar Blade's host destroys through it; the pinned handle is the shim-level one) */
  {HMODULE shim=GetModuleHandleW(L"amd_fidelityfx_dx12.dll");void*sd=shim&&shim!=module?reinterpret_cast<void*>(GetProcAddress(shim,"ffxDestroyContext")):nullptr;
   if(sd&&sd!=reinterpret_cast<void*>(destroy)){HMODULE pinned3=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(sd),&pinned3);
    auto s3=MH_CreateHook(sd,reinterpret_cast<void*>(&destroy_context_shim),reinterpret_cast<void**>(&original_destroy_shim));if(s3==MH_OK)s3=MH_EnableHook(sd);
    if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu shim_destroy_hook_status=%u target=%p\n",GetCurrentProcessId(),unsigned(s3),sd);fclose(f);}}}
 }
#ifdef NATIVE_ORDER_NEURAL
 if(!module){NativeMotionSign()=-1.f;cross_thread_submit=true;s=MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&xess_execute),reinterpret_cast<void**>(&original_xess));}else
#endif
 if(module){
  s=MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&dispatch_sr),reinterpret_cast<void**>(&original_sr));if(s==MH_OK)s=MH_EnableHook(reinterpret_cast<void*>(target));
  original=original_sr;
  /* the shim, when it is a different module with its own export: hook it too (Stellar Blade's host calls this one) */
  HMODULE shim=GetModuleHandleW(L"amd_fidelityfx_dx12.dll");void*shim_target=shim&&shim!=module?reinterpret_cast<void*>(GetProcAddress(shim,"ffxDispatch")):nullptr;
  if(s==MH_OK&&shim_target&&shim_target!=reinterpret_cast<void*>(target)){
   HMODULE pinned2=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(shim_target),&pinned2);
   auto s2=MH_CreateHook(shim_target,reinterpret_cast<void*>(&dispatch_shim),reinterpret_cast<void**>(&original_shim));if(s2==MH_OK)s2=MH_EnableHook(shim_target);
   if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu shim_hook_status=%u target=%p\n",GetCurrentProcessId(),unsigned(s2),shim_target);fclose(f);}
  }
 }else{s=MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&dispatch_sr),reinterpret_cast<void**>(&original_sr));if(s==MH_OK)s=MH_EnableHook(reinterpret_cast<void*>(target));original=original_sr;}
 // Do not retry an existing-hook conflict or modify another addon's hook.
 if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){wchar_t name[MAX_PATH]{};GetModuleFileNameW(module?module:xess,name,MAX_PATH);const wchar_t*base=wcsrchr(name,L'\\');fprintf(f,"pid=%lu hook_status=%u upscaler=%s module=%ls target=%p\n",GetCurrentProcessId(),unsigned(s),module?"ffx":"xess",base?base+1:name,target);
  /* diagnostic: where the other FFX dlls' ffxDispatch exports live (shim vs upscaler vs driver provider) */
  for(const wchar_t*other:{L"amd_fidelityfx_dx12.dll",L"amd_fidelityfx_upscaler_dx12.dll",L"amdxcffx64.dll"}){HMODULE m=GetModuleHandleW(other);void*fn=m?reinterpret_cast<void*>(GetProcAddress(m,"ffxDispatch")):nullptr;unsigned char head[8]{};if(fn)memcpy(head,fn,8);fprintf(f,"pid=%lu kind=ffx_export dll=%ls module=%p ffxDispatch=%p head=%02x%02x%02x%02x%02x%02x%02x%02x\n",GetCurrentProcessId(),other,m,fn,head[0],head[1],head[2],head[3],head[4],head[5],head[6],head[7]);}
  fclose(f);}return s==MH_OK?0:4;
}
// Before the game creates its D3D12 device: select the private Agility 721 runtime shipped in
// the game folder and enable the experimental shader-model feature so SM6.10 wave-matrix PSOs
// can be created on the game device. Gated by D:\DLSSNR-Lab\enable-game-sdk721.txt.
static bool on_create_device(reshade::api::device_api api,uint32_t&){
#if defined(NATIVE_ORDER_NEURAL)&&NATIVE_HAVE_SDKLAYERS
 if(api==reshade::api::device_api::d3d12&&NativePreUpscale::Enabled()){
  unsigned enable=0;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();sscanf(line,"DLSS5_PRE_UPSCALE_DEBUG=%u",&enable);}
  if(enable){
   /* This machine has the matching SDK layers in the old diagnostic Agility directory, not in Windows. */
   static bool sdk_attempted=false;if(!sdk_attempted){sdk_attempted=true;
    const GUID clsid={0x7cda6aca,0xa03e,0x49c8,{0x94,0x58,0x03,0x34,0xd2,0x0e,0x07,0xce}};
    using GetInterface=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto module=GetModuleHandleW(L"d3d12.dll");auto get=module?reinterpret_cast<GetInterface>(GetProcAddress(module,"D3D12GetInterface")):nullptr;
    ID3D12SDKConfiguration*cfg=nullptr;HRESULT hr=get?get(clsid,IID_PPV_ARGS(&cfg)):E_NOINTERFACE;
    if(SUCCEEDED(hr)){hr=cfg->SetSDKVersion(721,".\\DLSS5-D3D12-721\\");cfg->Release();}
    if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-pre-debug.txt").c_str(),L"ab")){fprintf(f,"diagnostic_sdk721=%08x\n",unsigned(hr));fclose(f);}
   }
   ID3D12Debug*debug=nullptr;auto hr=D3D12GetDebugInterface(IID_PPV_ARGS(&debug));if(SUCCEEDED(hr)){debug->EnableDebugLayer();debug->Release();}
   if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-pre-debug.txt").c_str(),L"ab")){fprintf(f,"debug_enable=%08x\n",unsigned(hr));fclose(f);}}
 }
#endif
#ifdef DLSS5_USE_HIP
 (void)api;return false; // HIP kernels do not require SM6.10 or a private Agility runtime.
#else
 if(api!=reshade::api::device_api::d3d12||GetFileAttributesW(NativeLabPath(L"enable-game-sdk721.txt").c_str())==INVALID_FILE_ATTRIBUTES)return false;
 static std::atomic<bool>attempted{false};if(attempted.exchange(true))return false;
 /* Diagnostic (D:\DLSSNR-Lab\enable-dred.txt): Device Removed Extended Data, breadcrumbs + page faults, dumped by the frame when initialization fails. */
 if(GetFileAttributesW(NativeLabPath(L"enable-dred.txt").c_str())!=INVALID_FILE_ATTRIBUTES){ID3D12DeviceRemovedExtendedDataSettings*dred=nullptr;HRESULT dh=D3D12GetDebugInterface(IID_PPV_ARGS(&dred));if(SUCCEEDED(dh)&&dred){dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);dred->Release();}
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu dred_enable hr=%08x\n",GetCurrentProcessId(),unsigned(dh));fclose(f);}}
 const GUID clsid={0x7cda6aca,0xa03e,0x49c8,{0x94,0x58,0x03,0x34,0xd2,0x0e,0x07,0xce}};
 using GetInterfaceFn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);HMODULE d3d=GetModuleHandleW(L"d3d12.dll");auto get_interface=d3d?reinterpret_cast<GetInterfaceFn>(GetProcAddress(d3d,"D3D12GetInterface")):nullptr;
 ID3D12SDKConfiguration*configuration=nullptr;HRESULT get=get_interface?get_interface(clsid,IID_PPV_ARGS(&configuration)):HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND),set=E_ABORT,experimental=E_ABORT;
 if(SUCCEEDED(get)){set=configuration->SetSDKVersion(721,".\\DLSS5-D3D12-721\\");configuration->Release();}
 /* (after SetSDKVersion so the Agility folder's d3d12SDKLayers.dll is used) Diagnostic (D:\DLSSNR-Lab\enable-d3d12-debug.txt): the D3D12 debug layer (d3d12SDKLayers.dll from the Agility folder); its messages are dumped when initialization fails. */
#if NATIVE_HAVE_SDKLAYERS
 if(GetFileAttributesW(NativeLabPath(L"enable-d3d12-debug.txt").c_str())!=INVALID_FILE_ATTRIBUTES){ID3D12Debug*dbg=nullptr;HRESULT bh=D3D12GetDebugInterface(IID_PPV_ARGS(&dbg));if(SUCCEEDED(bh)&&dbg){dbg->EnableDebugLayer();dbg->Release();}
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu debug_layer hr=%08x\n",GetCurrentProcessId(),unsigned(bh));fclose(f);}}
#endif
 if(SUCCEEDED(set)){const GUID feature={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};experimental=D3D12EnableExperimentalFeatures(1,&feature,nullptr,nullptr);}
 if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu sdk721_before_device get=%08x set=%08x experimental=%08x\n",GetCurrentProcessId(),unsigned(get),unsigned(set),unsigned(experimental));fclose(f);}
 return false;
#endif
}
// VRAM reservation (DLSS5_RESERVE_VRAM_MB=N in native-game-flags.txt): right after the game creates its device, hold N MB of
// video memory in a placeholder buffer so the game sizes its texture pool with that much less; the placeholder is released
// just before the network allocates its own buffers, which then take that room instead of pushing the game's textures out.
static ID3D12Resource*reserved_vram=nullptr;
void NativeReleaseReservedVram(){if(reserved_vram){reserved_vram->Release();reserved_vram=nullptr;if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu reserved_vram_released\n",GetCurrentProcessId());fclose(f);}}}
static void on_init_device(reshade::api::device*device){
 if(!device||device->get_api()!=reshade::api::device_api::d3d12||reserved_vram)return;
 unsigned long mb=0;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();if(!strncmp(line,"DLSS5_RESERVE_VRAM_MB=",22))mb=strtoul(line+22,nullptr,10);}
 if(!mb)return;auto*d=reinterpret_cast<ID3D12Device*>(device->get_native());
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(mb)<<20;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 HRESULT hr=d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&reserved_vram));
 if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu reserved_vram_mb=%lu hr=%08x\n",GetCurrentProcessId(),mb,unsigned(hr));fclose(f);}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
  /* Optional process filter: DLSS5_ONLY_EXE=<substring of the host exe path> makes every other process refuse the add-on (the old
     hard-coded SB-Win64-Shipping.exe / Ronin.exe list returned FALSE here = ReShade error 1114 in any other game). */
  if(const wchar_t*only=_wgetenv(L"DLSS5_ONLY_EXE"))if(*only&&!wcsstr(path,only))return FALSE;
  if(!reshade::register_addon(h))return FALSE;
  reshade::register_event<reshade::addon_event::create_device>(on_create_device);
  reshade::register_event<reshade::addon_event::init_device>(on_init_device);
  reshade::register_event<reshade::addon_event::close_command_list>(close_list);
  reshade::register_event<reshade::addon_event::execute_command_list>(execute);
  reshade::register_event<reshade::addon_event::dispatch>(compute);
  reshade::register_event<reshade::addon_event::draw>(draw);
  reshade::register_event<reshade::addon_event::draw_indexed>(draw_indexed);
  reshade::register_event<reshade::addon_event::barrier>(barrier);
  reshade::register_event<reshade::addon_event::present>(on_present);
  HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned))return FALSE;
  HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);
 }return TRUE;
}
