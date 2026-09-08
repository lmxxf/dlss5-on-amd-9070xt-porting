#pragma once
#include "native_game_frame.h"
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
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-game-oneshot.txt)",L"ab")){fprintf(f,"pid=%lu tick=%llu event=%s detail=%s\n",GetCurrentProcessId(),GetTickCount64(),event,detail);fclose(f);}
 }
 static void Save(unsigned long request,const wchar_t*label,const std::vector<unsigned char>&bytes){
#ifdef NATIVE_GAME_TILED_VERIFICATION
  if(request>1&&GetFileAttributesW(LR"(D:\DLSSNR-Lab\continuous-reset-preview.txt)")!=INVALID_FILE_ATTRIBUTES)return;
#endif
  wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\logs\neural-%lu-request-%lu-%ls.f16)",GetCurrentProcessId(),request,label);
  FILE*f=_wfopen(path,L"wb");if(!f)throw std::runtime_error("one-shot file open");auto n=fwrite(bytes.data(),1,bytes.size(),f);fclose(f);if(n!=bytes.size())throw std::runtime_error("one-shot short write");
 }
 static DWORD WINAPI Initialize(void*data){
  auto*task=static_cast<Init*>(data);auto*self=task->self;
  try{
   const wchar_t*noise_path=LR"(D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32)";
   std::ifstream f(noise_path,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=201326592)throw std::runtime_error("one-shot noise size");
   std::vector<float>noise(201326592/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(noise.data()),201326592))throw std::runtime_error("one-shot noise read");
   self->frame=new NativeGameFrame;
#ifdef NATIVE_GAME_TILED_VERIFICATION
   Log("build_mode","tiled verification; separate assets; reset-history only");
   // Runtime path selection: NAME=VALUE lines (DLSS5_* only) applied to the process
   // environment before the network is created, mirroring the validated test runner chain.
   {unsigned applied=0;if(FILE*flags=_wfopen(LR"(D:\DLSSNR-Lab\native-game-flags.txt)",L"rb")){char line[256];while(fgets(line,sizeof line,flags)){size_t n=strlen(line);while(n&&(line[n-1]=='\n'||line[n-1]=='\r'||line[n-1]==' '))line[--n]=0;if(n<8||strncmp(line,"DLSS5_",6)||!strchr(line,'='))continue;if(!_putenv(line))applied++;}fclose(flags);}
    Log("flags_applied",std::to_string(applied).c_str());}
   {const bool temporal_on=GetFileAttributesW(LR"(D:\DLSSNR-Lab\temporal-history.txt)")!=INVALID_FILE_ATTRIBUTES&&task->temporal.motion_width&&task->temporal.render_width;
    char text[160];snprintf(text,sizeof text,"temporal=%u motion=%ux%u render=%ux%u",temporal_on?1u:0u,task->temporal.motion_width,task->temporal.motion_height,task->temporal.render_width,task->temporal.render_height);Log("temporal_config",text);
    self->frame->Create(self->queue,task->source,noise,LR"(D:\DLSSNR-Lab\native-game-tiled-assets)",nullptr,temporal_on?&task->temporal:nullptr);}
#else
   self->frame->Create(self->queue,task->source,noise,LR"(D:\DLSSNR-Lab\native-color-frame-samegpu)");
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
  if(!armed_request&&GetFileAttributesW(LR"(D:\DLSSNR-Lab\continuous-every-frame.txt)")!=INVALID_FILE_ATTRIBUTES){if(last_request<100000000){armed_request=++last_request;every_frame=true;phase=2;}}
#endif
  auto now=GetTickCount64();if(now>=next_poll){
   next_poll=now+250;unsigned long pid=0,id=0;
#ifdef NATIVE_GAME_TILED_VERIFICATION
   // Slow visual demonstration only: each frame deliberately resets history.
   // Remove this file to return to explicit single-frame requests.
   if(GetFileAttributesW(LR"(D:\DLSSNR-Lab\continuous-reset-preview.txt)")!=INVALID_FILE_ATTRIBUTES&&!armed_request){
    if(last_request<1000000){armed_request=++last_request;phase=2;}
   }
#endif
   if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\neural-frame-request.txt)",L"rb")){
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
    frame->RebindSourceAfterCompletion(source);
    frame->ProcessSubmittedFrame(source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0,false,motion,reset);
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
