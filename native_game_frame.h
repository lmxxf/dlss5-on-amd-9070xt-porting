#pragma once
#include "native_game_rgb_input.h"
#include "native_actual_network70.h"
#include "native_game_rgb_output.h"

// Integration boundary, not a ReShade callback. The caller must establish the
// correct source/color contract and submit all input producers before Process.
// One stable source texture per instance; recreate after draining on resize.
class NativeGameFrame {
 struct Resources {
  NativeGameSubmission submit;
  NativeGameRgbInput input;
  NativeActualNetwork70 network;
  NativeGameRgbOutput output;
 };
 Resources*resources{};bool ready{},failed{};std::mutex mutex;
public:
 NativeGameFrame()=default;NativeGameFrame(const NativeGameFrame&)=delete;
 ~NativeGameFrame(){
  // Failure may mean an unfinished GPU submission. Retain the entire graph,
  // not just the command allocator, until process exit rather than risk UAF.
  if(!failed)delete resources;
 }
 void Create(ID3D12CommandQueue*queue,ID3D12Resource*source,
             const std::vector<float>&noise,const std::wstring&directory){
  std::lock_guard<std::mutex>guard(mutex);
  if(resources||!queue||!source)throw std::runtime_error("frame initialization contract");
  resources=new Resources;
  try{
   resources->submit.Create(queue);auto*d=resources->submit.Device();
   resources->input.Create(d,source,directory);
   resources->network.Create(d,resources->input.Tiles(),resources->input.PostBase(),noise,directory);
   resources->output.Create(d,resources->network.Output(),directory);ready=true;
  }catch(...){failed=true;throw;}
 }
 // Synchronizes input conversion -> all network chunks -> display copy on the
 // supplied queue. Both source and destination return to their original states.
 // Caller must serialize other users of source/target and validate SDR encoding.
 void ProcessSubmittedFrame(ID3D12Resource*target,D3D12_RESOURCE_STATES source_state,
                            D3D12_RESOURCE_STATES target_state,UINT seed){
  std::lock_guard<std::mutex>guard(mutex);
  if(!ready||failed||!target)throw std::runtime_error("frame unavailable");
  try{
   auto&r=*resources;
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){r.input.Record(c,source_state);});
   r.network.Run(r.submit,seed);
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){r.output.Record(c,target,target_state);});
  }catch(...){failed=true;throw;}
 }
};
