#pragma once
#include "native_device_identity.h"
#include "native_game_rgb_input.h"
#include "native_actual_network70.h"
#include "native_rgb_texture.h"

// Integration boundary, not a ReShade callback. The caller must establish the
// correct source/color contract and submit all input producers before Process.
// Rebind rotating same-size source only after completed frames; recreate on resize.
class NativeGameFrame {
 struct Resources {
  ID3D12Resource*original{}; // Kept alive by encode/decode resource references.
  NativeGameSubmission submit;
  NativeGameCodec encode;
  NativeGameRgbInput input;
  NativeActualNetwork70 network;
  NativeRgbTexture neural;
  NativeGameCodec decode;
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
             const std::vector<float>&noise,const std::wstring&directory,ID3D12Resource*temporal_rgb=nullptr){
  std::lock_guard<std::mutex>guard(mutex);
  if(resources||!queue||!source)throw std::runtime_error("frame initialization contract");
  resources=new Resources;
  try{
   resources->submit.Create(queue);auto*d=resources->submit.Device();
   resources->encode.Create(d,{source},directory);
   resources->original=source;
   resources->input.Create(d,resources->encode.Output(),directory);
   // Captured original post origin(-4,-4) corresponds to shift3.
   resources->network.Create(d,resources->input.Tiles(),resources->input.PostBase(),noise,directory,temporal_rgb,3);
   resources->neural.Create(d,resources->network.Output(),directory);
   resources->decode.Create(d,{resources->encode.Output(),resources->neural.Output(),source},directory);ready=true;
  }catch(...){failed=true;throw;}
 }
 void RebindSourceAfterCompletion(ID3D12Resource*source){
  // ProcessSubmittedFrame holds this mutex through all completion waits. Failed
  // frames are never eligible: timeout does not mean GPU work has completed.
  std::lock_guard<std::mutex>guard(mutex);
  if(!ready||failed||!source)throw std::runtime_error("frame rebind unavailable");
  if(source==resources->original)return;
  try{resources->encode.RebindInputAfterCompletion(0,source);resources->decode.RebindInputAfterCompletion(2,source);resources->original=source;}
  catch(...){failed=true;throw;}
 }
 // Synchronizes encode -> network -> FP16 bridge -> decode -> FP16 copy on the
 // supplied queue. Both source and destination return to their original states.
 // Caller must serialize other users and supply the captured linear mode1 input.
 // This is NOT a swapchain present hook. Target is the game's float16 NR result.
 // Temporal sampler producer, if enabled, must already be submitted on this queue;
 // history provenance/reset policy remain the caller's responsibility.
 void ProcessSubmittedFrame(ID3D12Resource*target,D3D12_RESOURCE_STATES source_state,
                            D3D12_RESOURCE_STATES target_state,UINT seed,bool temporal_enabled=false){
  std::lock_guard<std::mutex>guard(mutex);
  if(!ready||failed||!target)throw std::runtime_error("frame unavailable");
  if(target==resources->original&&source_state!=target_state)throw std::runtime_error("aliased frame texture states disagree");
  auto desc=target->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)throw std::runtime_error("frame target must be1080p FP16");
  ID3D12Device*owner=nullptr;auto hr=target->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr))throw std::runtime_error("frame target device query");bool same=NativeSameDevice(owner,resources->submit.Device());owner->Release();if(!same)throw std::runtime_error("frame target device mismatch");
  try{
   auto&r=*resources;
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){r.encode.Record(c,{source_state});r.input.Record(c,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
   r.network.Run(r.submit,seed,temporal_enabled);
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){
    r.neural.Record(c);r.decode.Record(c,{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,source_state});
    D3D12_RESOURCE_BARRIER b[2]{};for(auto&v:b)v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition={r.decode.Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    b[1].Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,target_state,D3D12_RESOURCE_STATE_COPY_DEST};
    c->ResourceBarrier(1,b);if(target_state!=D3D12_RESOURCE_STATE_COPY_DEST)c->ResourceBarrier(1,b+1);
    c->CopyResource(target,r.decode.Output());
    for(auto&v:b)std::swap(v.Transition.StateBefore,v.Transition.StateAfter);
    c->ResourceBarrier(1,b);if(target_state!=D3D12_RESOURCE_STATE_COPY_DEST)c->ResourceBarrier(1,b+1);
   });
  }catch(...){failed=true;throw;}
 }
};
