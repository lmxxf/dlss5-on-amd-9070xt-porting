#pragma once
/* d3d12sdklayers.h is missing from older mingw-w64 (Ubuntu 22.04); the debug-layer / InfoQueue diagnostics are then compiled out */
#if __has_include(<d3d12sdklayers.h>)
#include <d3d12sdklayers.h>
#define NATIVE_HAVE_SDKLAYERS 1
#else
#define NATIVE_HAVE_SDKLAYERS 0
#endif
#include "native_lab_paths.h"
#include "native_game_frame.h"
void NativeReleaseReservedVram();
#include "native_submitted_readback.h"
#include "native_frame_input_check.h"
#include <atomic>
#include <cstring>
#include <string>
#include <cstdio>
// Diagnostic milestone: one real game frame with explicit history reset.
// Not the final temporal renderer. Failed initialization/render is never retried.
/* DRED data interface (mingw's d3d12.h lacks it; layout from the Agility SDK header, IID 98931D33-5AE8-4791-AA3C-1A73A2934E71). Diagnostic only. */
struct NativeDredBreadcrumbNode{const char*a;const wchar_t*list_name;const char*b;const wchar_t*queue_name;ID3D12GraphicsCommandList*list;ID3D12CommandQueue*queue;UINT32 count;const UINT32*last;const UINT32*history;const NativeDredBreadcrumbNode*next;};
struct NativeDredAllocationNode{const char*a;const wchar_t*name;UINT32 type;const NativeDredAllocationNode*next;};
struct NativeDredBreadcrumbsOutput{const NativeDredBreadcrumbNode*head;};
struct NativeDredPageFaultOutput{D3D12_GPU_VIRTUAL_ADDRESS va;const NativeDredAllocationNode*existing;const NativeDredAllocationNode*freed;};
struct NativeDredData:public IUnknown{virtual HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput(NativeDredBreadcrumbsOutput*)=0;virtual HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput(NativeDredPageFaultOutput*)=0;};
static constexpr GUID NativeDredIid={0x98931D33,0x5AE8,0x4791,{0xAA,0x3C,0x1A,0x73,0xA2,0x93,0x4E,0x71}};
class NativeGameOneShot {
#ifdef NATIVE_BYPASS_TEST
 friend struct NativeBypassTestAccess;
#endif
 std::atomic<unsigned>phase{0}; // idle, initializing, ready, rendering, done, failed
 unsigned source_width{},source_height{},motion_width{},motion_height{},render_width{},render_height{};float experimental_motion_scale[2]{};
 NativeGameFrame*frame{};ID3D12CommandQueue*queue{};
 std::mutex request_mutex;unsigned long last_request{},armed_request{};ULONGLONG next_poll{};bool every_frame{};unsigned long every_frame_count{};ULONGLONG every_frame_tick{};bool bypass{},f6_down{};
 struct Init {NativeGameOneShot*self;ID3D12Resource*source;NativeGameFrame::TemporalConfig temporal;};
 static void Log(const char*event,const char*detail=""){
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=%s detail=%s\n",GetCurrentProcessId(),GetTickCount64(),event,detail);fclose(f);}
 }
 // Called only with request_mutex held; polling from capture keeps F6 usable
 // even when bypassed frames never reach WantsFrame/OnSubmitted.
 void PollBypassKeyLocked(bool down){if(down&&!f6_down){bypass=!bypass;Log("toggle",bypass?"F6: neural override OFF (original upscaler passthrough)":"F6: neural override ON");}f6_down=down;}
 static void Save(unsigned long request,const wchar_t*label,const std::vector<unsigned char>&bytes){
  if(!_wgetenv(L"DLSS5_DEBUG_DUMPS"))return; /* diagnostic only */
#ifdef NATIVE_GAME_TILED_VERIFICATION
  if(request>1&&GetFileAttributesW(NativeLabPath(L"continuous-reset-preview.txt").c_str())!=INVALID_FILE_ATTRIBUTES)return;
#endif
  wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,NativeLabPath(L"logs\\neural-%lu-request-%lu-%ls.f16").c_str(),GetCurrentProcessId(),request,label);
  FILE*f=_wfopen(path,L"wb");if(!f)throw std::runtime_error("one-shot file open");auto n=fwrite(bytes.data(),1,bytes.size(),f);fclose(f);if(n!=bytes.size())throw std::runtime_error("one-shot short write");
 }
 static DWORD WINAPI Initialize(void*data){
  auto*task=static_cast<Init*>(data);auto*self=task->self;
  try{
#ifdef NATIVE_GAME_TILED_VERIFICATION
   // Runtime path selection: NAME=VALUE lines (DLSS5_* only) applied to the process
   // environment before the network is created, mirroring the validated test runner chain.
   /* Three config layers (native_config_layers.h) merged; a key already in the process environment wins and is not touched. */
   {unsigned applied=0,kept=0,layers=0;const NativeConfig&env=NativeConfigSystemEnvironment();
    for(const auto&e:NativeConfigLoadDir(NativeLabRoot(),&layers)){if(NativeConfigFind(env,e.key)){kept++;continue;}if(!_putenv((e.key+"="+e.value).c_str()))applied++;}
    Log("flags_applied",(std::to_string(applied)+" layers="+std::to_string(layers)+" env_kept="+std::to_string(kept)).c_str());}
#endif
   std::vector<float>noise;
#ifdef DLSS5_USE_HIP
   if(!hip_reference::FastPrefixFromEnvironment())
#endif
   {
   std::wstring noise_file=NativeLabPath(L"native-game-tiled-assets\\noise.f32");if(GetFileAttributesW(noise_file.c_str())==INVALID_FILE_ATTRIBUTES)noise_file=NativeLabPath(L"matrix-probe\\native-runtime-rgb512\\functions.f32");
   std::vector<char>noise_bytes;if(!NativeReadFile(noise_file,noise_bytes)||noise_bytes.size()!=201326592)throw std::runtime_error("one-shot noise size");
   noise.resize(201326592/4);std::memcpy(noise.data(),noise_bytes.data(),201326592);noise_bytes.clear();noise_bytes.shrink_to_fit();
   }
   self->frame=new NativeGameFrame;
#ifdef NATIVE_GAME_TILED_VERIFICATION
   Log("build_mode","tiled verification; separate assets; reset-history only");
   {const bool experiment=NativeTemporalExperimentRequested()||NativeFastHistoryRequested();const bool temporal_on=(experiment?(NativeTemporalExperimentUnjittered()&&task->temporal.experimental_ffx):GetFileAttributesW(NativeLabPath(L"temporal-history.txt").c_str())!=INVALID_FILE_ATTRIBUTES)&&task->temporal.motion_width>=2&&task->temporal.motion_height>=2&&task->temporal.render_width&&task->temporal.render_height;
    if(experiment){const char*reason=!NativeTemporalExperimentUnjittered()?"mv-unjittered-contract-not-admitted":!task->temporal.experimental_ffx?"requires-ffx-pre-upscale-metadata":temporal_on?"ffx-unjittered-assumption-admitted-backend-gates-pending":"invalid-motion-render-geometry";Log("temporal_history_experiment",(std::string("requested=1 eligible=")+std::to_string(temporal_on)+" reason="+reason).c_str());}
    char text[160];snprintf(text,sizeof text,"temporal=%u motion=%ux%u render=%ux%u",temporal_on?1u:0u,task->temporal.motion_width,task->temporal.motion_height,task->temporal.render_width,task->temporal.render_height);Log("temporal_config",text);
    NativeReleaseReservedVram();
    self->frame->Create(self->queue,task->source,noise,NativeLabPath(L"native-game-tiled-assets").c_str(),nullptr,temporal_on?&task->temporal:nullptr);}
#else
   self->frame->Create(self->queue,task->source,noise,NativeLabPath(L"native-color-frame-samegpu").c_str());
#endif
   Log("ready","await explicit PID/request-id; history_reset=1; not temporal acceptance");
   self->phase.store(2,std::memory_order_release);
  }catch(const std::exception&e){Log("initialization_failed",e.what());{ID3D12Device*d=nullptr;if(self->queue&&SUCCEEDED(self->queue->GetDevice(IID_PPV_ARGS(&d)))){char t[64];snprintf(t,sizeof t,"%08x",unsigned(d->GetDeviceRemovedReason()));Log("device_removed_reason",t);
#if NATIVE_HAVE_SDKLAYERS
     {ID3D12InfoQueue*q=nullptr;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&q)))&&q){UINT64 n=q->GetNumStoredMessages();if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-d3d12-debug.txt").c_str(),L"ab")){fprintf(f,"pid=%lu stored_messages=%llu\n",GetCurrentProcessId(),(unsigned long long)n);for(UINT64 i=n>40?n-40:0;i<n;i++){SIZE_T len=0;q->GetMessage(i,nullptr,&len);std::vector<char>buf(len+1);auto*m=reinterpret_cast<D3D12_MESSAGE*>(buf.data());if(len&&SUCCEEDED(q->GetMessage(i,m,&len)))fprintf(f,"  [%u/%u] %s\n",unsigned(m->Severity),unsigned(m->ID),m->pDescription?m->pDescription:"");}fclose(f);}q->Release();}}
#endif
     NativeDredData*dred=nullptr;if(SUCCEEDED(d->QueryInterface(NativeDredIid,reinterpret_cast<void**>(&dred)))&&dred){NativeDredBreadcrumbsOutput bc{};NativeDredPageFaultOutput pf{};HRESULT h1=dred->GetAutoBreadcrumbsOutput(&bc),h2=dred->GetPageFaultAllocationOutput(&pf);
      if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-dred.txt").c_str(),L"ab")){fprintf(f,"pid=%lu breadcrumbs_hr=%08x pagefault_hr=%08x fault_va=%llx\n",GetCurrentProcessId(),unsigned(h1),unsigned(h2),(unsigned long long)pf.va);
       if(SUCCEEDED(h2)){for(auto*n=pf.existing;n;n=n->next)fprintf(f,"  existing alloc type=%u name=%ls\n",unsigned(n->type),n->name?n->name:L"");for(auto*n=pf.freed;n;n=n->next)fprintf(f,"  freed alloc type=%u name=%ls\n",unsigned(n->type),n->name?n->name:L"");}
       if(SUCCEEDED(h1)){unsigned lists=0;for(auto*n=bc.head;n&&lists<256;n=n->next,lists++){unsigned done=n->last?*n->last:0;if(done==n->count)continue;fprintf(f,"  list=%p (%ls) queue=%p (%ls) count=%u completed=%u ops:",n->list,n->list_name?n->list_name:L"",n->queue,n->queue_name?n->queue_name:L"",n->count,done);for(unsigned i=done>6?done-6:0;i<n->count&&i<done+6;i++)fprintf(f," %u%s",unsigned(n->history[i]),i==done?"<":"");fputc('\n',f);}}
       fclose(f);}dred->Release();}
     d->Release();}}self->phase.store(5);}
  task->source->Release();delete task;return 0;
 }
public:
 bool Bypassed(){
  std::lock_guard<std::mutex>guard(request_mutex);
#ifdef NATIVE_GAME_TILED_VERIFICATION
  const unsigned state=phase.load(std::memory_order_acquire);
  if(state==2||state==4)PollBypassKeyLocked((GetAsyncKeyState(VK_F6)&0x8000)!=0);
#endif
  return bypass;
 }
 bool WantsFrame(){
  std::lock_guard<std::mutex>guard(request_mutex);
  unsigned state=phase.load(std::memory_order_acquire);if(state!=2&&state!=4)return false;
#ifdef NATIVE_GAME_TILED_VERIFICATION
  // F6 toggles the neural override (edge-triggered); while bypassed the game's own FSR output is shown.
  PollBypassKeyLocked((GetAsyncKeyState(VK_F6)&0x8000)!=0);
  if(bypass)return false;
  // Every-frame mode: replace every FSR frame synchronously (coherent picture at network speed).
  // Still resets history each frame; remove the file to fall back to the polled slow preview.
  if(!armed_request&&GetFileAttributesW(NativeLabPath(L"continuous-every-frame.txt").c_str())!=INVALID_FILE_ATTRIBUTES){if(last_request<100000000){armed_request=++last_request;every_frame=true;phase=2;}}
#endif
  auto now=GetTickCount64();if(now>=next_poll){
   next_poll=now+250;unsigned long pid=0,id=0;
#ifdef NATIVE_GAME_TILED_VERIFICATION
   // Slow visual demonstration only: each frame deliberately resets history.
   // Remove this file to return to explicit single-frame requests.
   if(GetFileAttributesW(NativeLabPath(L"continuous-reset-preview.txt").c_str())!=INVALID_FILE_ATTRIBUTES&&!armed_request){
    if(last_request<1000000){armed_request=++last_request;phase=2;}
   }
#endif
   if(FILE*f=_wfopen(NativeLabPath(L"neural-frame-request.txt").c_str(),L"rb")){
    int fields=fscanf(f,"%lu %lu",&pid,&id);fclose(f);
    if(fields==2&&NativeFrameRequestValid(GetCurrentProcessId(),pid,id,last_request)&&!armed_request){last_request=id;armed_request=id;phase=2;Log("request_armed",std::to_string(id).c_str());}
   }
  }
  return phase.load()==2&&armed_request!=0;
 }
 unsigned Phase()const{return phase.load(std::memory_order_acquire);}
 /* steady-state frame interval (ms, average of the last 100 frames; 0 until then) for the on-screen fps (DLSS5_SHOW_FPS) */
 std::atomic<double>avg_ms{0.0};double AvgMs()const{return avg_ms.load();}
 /* A new upscaler session (Magpie: scaling stopped and started again -> new FSR context, queue and textures; or a failed initialization)
    drops the frame and goes back to idle, so the next snapshot initializes again on the new queue. Bounded: at most 8 restarts after failures per process; geometry changes are unbounded. */
 unsigned restarts{};
 bool ResetForNewSession(const char*why){
  unsigned state=phase.load();if(state==1||state==3)return false;if(state==5){if(restarts>=8)return false;restarts++;} /* 2026-09-25: only failed sessions spend the restart budget; geometry/queue changes (preset switches) are free */
  std::lock_guard<std::mutex>guard(request_mutex);
  delete frame;frame=nullptr;if(queue){queue->Release();queue=nullptr;}
  avg_ms.store(0.0);
  armed_request=0;last_request=0;every_frame=false;every_frame_count=0;next_poll=0;
  Log("session_reset",why);phase.store(0,std::memory_order_release);return true;
 }
 /* DLSS5_DIRECT_IO bit 2: set by the steady-state every-frame path when the result was NOT copied into source; the caller feeds this
    texture (NON_PIXEL_SHADER_RESOURCE) to the upscaler instead. nullptr: the result is in source as before. */
 ID3D12Resource*delivered{};ID3D12Resource*Delivered()const{return delivered;}
 /* state: the D3D12 state the upscaler declared for its output (the frame transitions from it and back to it) */
 void OnSubmitted(ID3D12CommandQueue*q,ID3D12Resource*source,ID3D12Resource*motion=nullptr,bool reset=false,unsigned mw=0,unsigned mh=0,unsigned rw=0,unsigned rh=0,D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS,bool external_overlay=false,bool direct_output=false,const NativeTemporalFrameMetadata*experimental=nullptr,ID3D12Resource*depth=nullptr,D3D12_RESOURCE_STATES depth_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE){
  delivered=nullptr;
  const bool experiment=NativeTemporalExperimentRequested()||NativeFastHistoryRequested();const bool experiment_metadata=experiment&&experimental&&experimental->Valid();
  {const unsigned state=phase.load();if(state==5||((state==2||state==4)&&queue&&q!=queue)){if(!ResetForNewSession(state==5?"previous initialization/render failed":"upscaler queue changed"))return;}}
  if(source&&(phase.load()==2||phase.load()==4)){
   auto desc=source->GetDesc();
   if(desc.Width!=source_width||desc.Height!=source_height||mw!=motion_width||mh!=motion_height||rw!=render_width||rh!=render_height||(experiment_metadata&&(experimental->motion_scale[0]!=experimental_motion_scale[0]||experimental->motion_scale[1]!=experimental_motion_scale[1])))
    if(!ResetForNewSession("input or motion geometry changed"))return;
  }
  unsigned expected=0;
  if(phase.compare_exchange_strong(expected,1)){
   if(!q||!source||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT){Log("initialization_failed","queue/source");phase=5;return;}
   Init*task=nullptr;
   try{task=new Init{this,source,{mw,mh,rw,rh,experiment_metadata,experiment_metadata?experimental->motion_scale[0]:0.f,experiment_metadata?experimental->motion_scale[1]:0.f}};}catch(const std::exception&e){Log("initialization_failed",e.what());phase=5;return;}
   source_width=unsigned(source->GetDesc().Width);source_height=source->GetDesc().Height;motion_width=mw;motion_height=mh;render_width=rw;render_height=rh;if(experiment_metadata){experimental_motion_scale[0]=experimental->motion_scale[0];experimental_motion_scale[1]=experimental->motion_scale[1];}
   queue=q;queue->AddRef();source->AddRef();
   HANDLE thread=CreateThread(nullptr,0,Initialize,task,0,nullptr);
   if(!thread){source->Release();delete task;Log("initialization_failed","thread creation");phase=5;return;}
   CloseHandle(thread);Log("initialization_started","background; game output unchanged");return;
  }
  expected=2;if(!phase.compare_exchange_strong(expected,3))return;
  unsigned long request=0;
  {std::lock_guard<std::mutex>guard(request_mutex);request=armed_request;armed_request=0;}
  if(!request){phase=2;return;}
  try{
   if(experiment){frame->ExperimentalFrameMetadata(experiment_metadata?*experimental:NativeTemporalFrameMetadata{});}
   if(external_overlay)frame->SuppressFps();
   if(q!=queue)throw std::runtime_error("one-shot queue changed"); /* caught below -> phase 5 -> ResetForNewSession on the next dispatch */
   if(every_frame&&request>1){
    // Steady state: no readback, no files; one log line per 100 frames with the average interval.
    // Temporal alignment probe: dump history/motion/color/warped at frames 300 and 600 while the user pans the camera.
    // Flicker probe (DLSS5_FLICKER_DUMP=<first frame>): dump five consecutive frames (history = previous output, color =
    // this frame's input) so input vs output frame-to-frame differences can be compared offline.
    /* 2026-09-24 (Cyberpunk): the request-1 one-shot dump is the load-in frame. With DLSS5_DEBUG_DUMPS, also dump the game colour
       input of the 600th every-frame call (gameplay) so the scene's linear range can be measured offline (logs\neural-<pid>-request-<n>-frame600.f16). */
    {static const unsigned long dump_at=[]{const wchar_t*v=_wgetenv(L"DLSS5_DUMP_FRAME");unsigned long n=v?wcstoul(v,nullptr,10):600ul;return n?n:600ul;}(); /* DLSS5_DUMP_FRAME=<n>: which every-frame call to dump (default 600) */
     if(every_frame_count+1==dump_at&&_wgetenv(L"DLSS5_DEBUG_DUMPS")){auto b=NativeReadSubmittedFrame(q,source,state);wchar_t label[32];swprintf(label,32,L"frame%lu",dump_at);Save(request,label,b);Log("frame_dump","game colour input written");}}
    {static long flicker_first=[]{const wchar_t*v=_wgetenv(L"DLSS5_FLICKER_DUMP");return v?wcstol(v,nullptr,10):-1L;}();
     const long index=long(every_frame_count)+1;
     if(flicker_first>0&&index>=flicker_first&&index<flicker_first+5){wchar_t prefix[MAX_PATH];swprintf(prefix,MAX_PATH,NativeLabPath(L"logs\\flicker-%lu-%ld").c_str(),GetCurrentProcessId(),index);frame->RequestTemporalDump(prefix);Log("flicker_dump_requested",std::to_string(index).c_str());}}
    if((every_frame_count==299||every_frame_count==599)&&_wgetenv(L"DLSS5_DEBUG_DUMPS")){ /* diagnostic only (233MB); DLSS5_DEBUG_DUMPS=1 in native-game-flags.txt */wchar_t prefix[MAX_PATH];swprintf(prefix,MAX_PATH,NativeLabPath(L"logs\\temporal-probe-%lu-%lu").c_str(),GetCurrentProcessId(),every_frame_count+1);frame->RequestTemporalDump(prefix);Log("temporal_dump_requested",std::to_string(every_frame_count+1).c_str());}
    frame->RebindSourceAfterCompletion(source);
    frame->UpdateFps(AvgMs());
    {ID3D12Resource*out=direct_output?frame->DirectOutput():nullptr;frame->ProcessSubmittedFrame(source,state,state,0,false,motion,reset,out==nullptr,depth,depth_state);delivered=out;}
    // Temporal alignment probe: dump history/motion/color at frames 300 and 600 while the user pans the camera.
    auto now=GetTickCount64();if(++every_frame_count%100==0){char text[96];snprintf(text,sizeof text,"frames=%lu avg_ms_per_frame=%.1f",every_frame_count,double(now-every_frame_tick)/100.0);Log("every_frame",text);avg_ms.store(double(now-every_frame_tick)/100.0);every_frame_tick=now;}
    if(every_frame_count==1)every_frame_tick=now;
    phase=4;return;
   }
   auto before=NativeReadSubmittedFrame(q,source,state);Save(request,L"before",before);
   auto input_desc=source->GetDesc();auto input_check=CheckNativeFrameInput(before,unsigned(input_desc.Width),input_desc.Height,NativeBytesPerPixel(input_desc.Format));
   if(input_check!=NativeFrameInputCheck::valid){Log("input_rejected",input_check==NativeFrameInputCheck::black?"black RGB; no neural write; new request required":"invalid input; no neural write; new request required");phase=2;return;}
   frame->RebindSourceAfterCompletion(source);
   Log("render_begin",frame->TemporalReady()?"seed=0 temporal path armed (history from previous processed frame)":"seed=0 history=0 explicit diagnostic reset");
   frame->ProcessSubmittedFrame(source,state,state,0,false,motion,reset,true,depth,depth_state);
   auto after=NativeReadSubmittedFrame(q,source,state);Save(request,L"after",after);
   Log("render_complete",before==after?"pixels_identical; investigate":"pixels_changed; independent verification pending");phase=4;
  }catch(const std::exception&e){Log("render_failed",e.what());phase=5;}
 }
};
