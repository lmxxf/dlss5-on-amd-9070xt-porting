#pragma once
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
class NativeGameOneShot {
 std::atomic<unsigned>phase{0}; // idle, initializing, ready, rendering, done, failed
 NativeGameFrame*frame{};ID3D12CommandQueue*queue{};
 std::mutex request_mutex;unsigned long last_request{},armed_request{};ULONGLONG next_poll{};bool every_frame{};unsigned long every_frame_count{};ULONGLONG every_frame_tick{};bool bypass{},f6_down{};
 struct Init {NativeGameOneShot*self;ID3D12Resource*source;NativeGameFrame::TemporalConfig temporal;};
 static void Log(const char*event,const char*detail=""){
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=%s detail=%s\n",GetCurrentProcessId(),GetTickCount64(),event,detail);fclose(f);}
 }
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
   std::wstring noise_file=NativeLabPath(L"native-game-tiled-assets\\noise.f32");if(GetFileAttributesW(noise_file.c_str())==INVALID_FILE_ATTRIBUTES)noise_file=NativeLabPath(L"matrix-probe\\native-runtime-rgb512\\functions.f32");const wchar_t*noise_path=noise_file.c_str();
   std::ifstream f(noise_path,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=201326592)throw std::runtime_error("one-shot noise size");
   std::vector<float>noise(201326592/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(noise.data()),201326592))throw std::runtime_error("one-shot noise read");
   self->frame=new NativeGameFrame;
#ifdef NATIVE_GAME_TILED_VERIFICATION
   Log("build_mode","tiled verification; separate assets; reset-history only");
   // Runtime path selection: NAME=VALUE lines (DLSS5_* only) applied to the process
   // environment before the network is created, mirroring the validated test runner chain.
   {unsigned applied=0;if(FILE*flags=_wfopen(NativeLabPath(L"native-game-flags.txt").c_str(),L"rb")){char line[256];while(fgets(line,sizeof line,flags)){size_t n=strlen(line);while(n&&(line[n-1]=='\n'||line[n-1]=='\r'||line[n-1]==' '))line[--n]=0;if(n<8||strncmp(line,"DLSS5_",6)||!strchr(line,'='))continue;if(!_putenv(line))applied++;}fclose(flags);}
    Log("flags_applied",std::to_string(applied).c_str());}
   {const bool temporal_on=GetFileAttributesW(NativeLabPath(L"temporal-history.txt").c_str())!=INVALID_FILE_ATTRIBUTES&&task->temporal.motion_width&&task->temporal.render_width;
    char text[160];snprintf(text,sizeof text,"temporal=%u motion=%ux%u render=%ux%u",temporal_on?1u:0u,task->temporal.motion_width,task->temporal.motion_height,task->temporal.render_width,task->temporal.render_height);Log("temporal_config",text);
    NativeReleaseReservedVram();
    self->frame->Create(self->queue,task->source,noise,NativeLabPath(L"native-game-tiled-assets").c_str(),nullptr,temporal_on?&task->temporal:nullptr);}
#else
   self->frame->Create(self->queue,task->source,noise,NativeLabPath(L"native-color-frame-samegpu").c_str());
#endif
   Log("ready","await explicit PID/request-id; history_reset=1; not temporal acceptance");
   self->phase.store(2,std::memory_order_release);
  }catch(const std::exception&e){Log("initialization_failed",e.what());self->phase.store(5);}
  task->source->Release();delete task;return 0;
 }
public:
 bool WantsFrame(){
  std::lock_guard<std::mutex>guard(request_mutex);
  unsigned state=phase.load(std::memory_order_acquire);if(state!=2&&state!=4)return false;
#ifdef NATIVE_GAME_TILED_VERIFICATION
  // F6 toggles the neural override (edge-triggered); while bypassed the game's own FSR output is shown.
  {bool down=(GetAsyncKeyState(VK_F6)&0x8000)!=0;if(down&&!f6_down){bypass=!bypass;Log("toggle",bypass?"F6: neural override OFF (game FSR)":"F6: neural override ON");}f6_down=down;}
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
 void OnSubmitted(ID3D12CommandQueue*q,ID3D12Resource*source,ID3D12Resource*motion=nullptr,bool reset=false,unsigned mw=0,unsigned mh=0,unsigned rw=0,unsigned rh=0){
  unsigned expected=0;
  if(phase.compare_exchange_strong(expected,1)){
   if(!q||!source||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT){Log("initialization_failed","queue/source");phase=5;return;}
   Init*task=nullptr;
   try{task=new Init{this,source,{mw,mh,rw,rh}};}catch(const std::exception&e){Log("initialization_failed",e.what());phase=5;return;}
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
   if(q!=queue)throw std::runtime_error("one-shot queue changed");
   if(every_frame&&request>1){
    // Steady state: no readback, no files; one log line per 100 frames with the average interval.
    // Temporal alignment probe: dump history/motion/color/warped at frames 300 and 600 while the user pans the camera.
    // Flicker probe (DLSS5_FLICKER_DUMP=<first frame>): dump five consecutive frames (history = previous output, color =
    // this frame's input) so input vs output frame-to-frame differences can be compared offline.
    {static long flicker_first=[]{const wchar_t*v=_wgetenv(L"DLSS5_FLICKER_DUMP");return v?wcstol(v,nullptr,10):-1L;}();
     const long index=long(every_frame_count)+1;
     if(flicker_first>0&&index>=flicker_first&&index<flicker_first+5){wchar_t prefix[MAX_PATH];swprintf(prefix,MAX_PATH,NativeLabPath(L"logs\\flicker-%lu-%ld").c_str(),GetCurrentProcessId(),index);frame->RequestTemporalDump(prefix);Log("flicker_dump_requested",std::to_string(index).c_str());}}
    if((every_frame_count==299||every_frame_count==599)&&_wgetenv(L"DLSS5_DEBUG_DUMPS")){ /* diagnostic only (233MB); DLSS5_DEBUG_DUMPS=1 in native-game-flags.txt */wchar_t prefix[MAX_PATH];swprintf(prefix,MAX_PATH,NativeLabPath(L"logs\\temporal-probe-%lu-%lu").c_str(),GetCurrentProcessId(),every_frame_count+1);frame->RequestTemporalDump(prefix);Log("temporal_dump_requested",std::to_string(every_frame_count+1).c_str());}
    frame->RebindSourceAfterCompletion(source);
    frame->ProcessSubmittedFrame(source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0,false,motion,reset);
    // Temporal alignment probe: dump history/motion/color at frames 300 and 600 while the user pans the camera.
    auto now=GetTickCount64();if(++every_frame_count%100==0){char text[96];snprintf(text,sizeof text,"frames=%lu avg_ms_per_frame=%.1f",every_frame_count,double(now-every_frame_tick)/100.0);Log("every_frame",text);every_frame_tick=now;}
    if(every_frame_count==1)every_frame_tick=now;
    phase=4;return;
   }
   auto before=NativeReadSubmittedFrame(q,source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Save(request,L"before",before);
   auto input_check=CheckNativeFrameInput(before);
   if(input_check!=NativeFrameInputCheck::valid){Log("input_rejected",input_check==NativeFrameInputCheck::black?"black RGB; no neural write; new request required":"invalid input; no neural write; new request required");phase=2;return;}
   frame->RebindSourceAfterCompletion(source);
   Log("render_begin",frame->TemporalReady()?"seed=0 temporal path armed (history from previous processed frame)":"seed=0 history=0 explicit diagnostic reset");
   frame->ProcessSubmittedFrame(source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0,false,motion,reset);
   auto after=NativeReadSubmittedFrame(q,source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Save(request,L"after",after);
   Log("render_complete",before==after?"pixels_identical; investigate":"pixels_changed; independent verification pending");phase=4;
  }catch(const std::exception&e){Log("render_failed",e.what());phase=5;}
 }
};
