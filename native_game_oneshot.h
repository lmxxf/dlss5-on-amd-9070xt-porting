#pragma once
#include "native_game_frame.h"
#include "native_submitted_readback.h"
#include <atomic>
#include <cstdio>
// Diagnostic milestone: one real game frame with explicit history reset.
// Not the final temporal renderer. Failed initialization/render is never retried.
class NativeGameOneShot {
 std::atomic<unsigned>phase{0}; // idle, initializing, ready, rendering, done, failed
 NativeGameFrame*frame{};ID3D12CommandQueue*queue{};
 struct Init {NativeGameOneShot*self;ID3D12Resource*source;};
 static void Log(const char*event,const char*detail=""){
  if(FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\native-game-oneshot.txt)",L"ab")){fprintf(f,"pid=%lu tick=%llu event=%s detail=%s\n",GetCurrentProcessId(),GetTickCount64(),event,detail);fclose(f);}
 }
 static void Save(const wchar_t*label,const std::vector<unsigned char>&bytes){
  wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,LR"(D:\DLSSNR-Lab\logs\neural-%lu-%ls.f16)",GetCurrentProcessId(),label);
  FILE*f=_wfopen(path,L"wb");if(!f)throw std::runtime_error("one-shot file open");auto n=fwrite(bytes.data(),1,bytes.size(),f);fclose(f);if(n!=bytes.size())throw std::runtime_error("one-shot short write");
 }
 static DWORD WINAPI Initialize(void*data){
  auto*task=static_cast<Init*>(data);auto*self=task->self;
  try{
   const wchar_t*noise_path=LR"(D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32)";
   std::ifstream f(noise_path,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=201326592)throw std::runtime_error("one-shot noise size");
   std::vector<float>noise(201326592/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(noise.data()),201326592))throw std::runtime_error("one-shot noise read");
   self->frame=new NativeGameFrame;
   self->frame->Create(self->queue,task->source,noise,LR"(D:\DLSSNR-Lab\native-color-frame-samegpu)");
   Log("ready","next matched frame; history_reset=1; not temporal acceptance");
   self->phase.store(2,std::memory_order_release);
  }catch(const std::exception&e){Log("initialization_failed",e.what());self->phase.store(5);}
  task->source->Release();delete task;return 0;
 }
public:
 bool WantsFrame()const{return phase.load(std::memory_order_acquire)==2;}
 void OnSubmitted(ID3D12CommandQueue*q,ID3D12Resource*source){
  unsigned expected=0;
  if(phase.compare_exchange_strong(expected,1)){
   if(!q||!source||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT){Log("initialization_failed","queue/source");phase=5;return;}
   Init*task=nullptr;
   try{task=new Init{this,source};}catch(const std::exception&e){Log("initialization_failed",e.what());phase=5;return;}
   queue=q;queue->AddRef();source->AddRef();
   HANDLE thread=CreateThread(nullptr,0,Initialize,task,0,nullptr);
   if(!thread){source->Release();delete task;Log("initialization_failed","thread creation");phase=5;return;}
   CloseHandle(thread);Log("initialization_started","background; game output unchanged");return;
  }
  expected=2;if(!phase.compare_exchange_strong(expected,3))return;
  try{
   if(q!=queue)throw std::runtime_error("one-shot queue changed");
   auto before=NativeReadSubmittedFrame(q,source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Save(L"before",before);
   frame->RebindSourceAfterCompletion(source);
   Log("render_begin","seed=0 history=0 explicit diagnostic reset");
   frame->ProcessSubmittedFrame(source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0,false);
   auto after=NativeReadSubmittedFrame(q,source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Save(L"after",after);
   Log("render_complete",before==after?"pixels_identical; investigate":"pixels_changed; independent verification pending");phase=4;
  }catch(const std::exception&e){Log("render_failed",e.what());phase=5;}
 }
};
