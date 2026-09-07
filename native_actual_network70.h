#pragma once
#include "native_device_identity.h"
#include "native_c32_ds.h"
#include "native_vit_block.h"
#include "native_actual_decoder69.h"
#include "native_post70.h"
#include "native_game_submission.h"
#include "native_network_timestamps.h"

// Actual processing extent, with externally supplied GPU RGB tiles and HWC base.
// No fixture activations, oracles, game command lists, or CPU pixel readbacks.
class NativeActualNetwork70 {
 NativePreblockRuntime pre;
 NativeC32Stage c32[4];NativeC64Shift c64[4],c128[6],c256[8];
 NativeC32Downsample ds4,ds8,ds14,ds22,head;
 NativeSplitWindow split[8];NativeVitGather bridge;NativeVitBlock vit[8];
 NativeActualDecoder69 decoder;NativePost70 post;
 ID3D12Device*device{};bool ready{},failed{},temporal_bound{};
 NativeNetworkTimestamps timestamps;bool profile{};
 static std::vector<float>Read(const std::wstring&path){
  std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("network coefficient missing");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("network coefficient size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("network coefficient truncated");return v;
 }
public:
 NativeActualNetwork70()=default;NativeActualNetwork70(const NativeActualNetwork70&)=delete;
 ~NativeActualNetwork70(){if(device)device->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*rgb_tiles,ID3D12Resource*rgb_hwc,
             const std::vector<float>&noise,const std::wstring&dir,ID3D12Resource*temporal_rgb=nullptr,UINT post_shift=0){
  if(post_shift>3)throw std::runtime_error("network post shift");
  if(device||!d||!rgb_tiles||!rgb_hwc||noise.size()!=201326592/4)throw std::runtime_error("network initialization contract");
  if(_wgetenv(L"DLSS5_POST_BASE_ONLY"))throw std::runtime_error("diagnostic post forbidden");
  for(auto*r:{rgb_tiles,rgb_hwc,temporal_rgb}){
   if(!r)continue;
   if(r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<1920ull*1152*16)throw std::runtime_error("network RGB capacity");
   ID3D12Device*owner=nullptr;auto hr=r->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr))throw std::runtime_error("network RGB device query");bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("network RGB device mismatch");
  }
  device=d;device->AddRef();auto read=[&](const std::wstring&name){return Read(dir+L"\\"+name);};
  if(const wchar_t*v=_wgetenv(L"DLSS5_NETWORK_GPU_PROFILE")){if(wcscmp(v,L"1"))throw std::runtime_error("invalid network profile flag");profile=true;timestamps.Create(d);}
  pre.Create(d,rgb_tiles,1920,1152,read(L"block0-ffn.f32"),read(L"block0-attention.f32"),dir,true,false,&noise,temporal_rgb);temporal_bound=temporal_rgb!=nullptr;
  const UINT shifts[]={0,3,1,2,0,3,1,2};auto*source=pre.Downsample();
  for(UINT i=0;i<4;i++){auto p=L"block"+std::to_wstring(i+1);c32[i].Create(d,source,960,576,shifts[i],read(p+L"-ffn.f32"),read(p+L"-attention.f32"),dir);source=c32[i].Output();}
  ds4.Create(d,c32[3].PooledWork(),960,576,2,read(L"block4-ds.f32"),dir);source=ds4.Output();
  auto group=[&](NativeC64Shift*layers,UINT count,UINT first,UINT w,UINT h,UINT channels,NativeC32Downsample&ds){
   for(UINT i=0;i<count;i++){auto p=L"block"+std::to_wstring(first+i);layers[i].Create(d,source,w,h,shifts[i],read(p+L"-ffn.f32"),read(p+L"-attention.f32"),dir,i+1==count,channels);source=layers[i].Output();}
   ds.Create(d,source,w,h,0,read(L"block"+std::to_wstring(first+count-1)+L"-ds.f32"),dir,true,channels);source=ds.Output();
  };
  group(c64,4,5,480,288,64,ds8);group(c128,6,9,240,144,128,ds14);group(c256,8,15,120,72,256,ds22);
  for(UINT i=0;i<8;i++){auto p=L"block"+std::to_wstring(23+i);split[i].Create(d,source,60,36,shifts[i],read(p+L"-ffwd.f32"),read(p+L"-ffwd-projection.f32"),read(p+L"-attention.f32"),dir,i==7);source=split[i].Output();}
  head.Create(d,source,60,36,0,read(L"head-matrix.f32"),dir,true,512);
  auto rawmap=read(L"hwc-to-vit.i32");if(rawmap.size()!=655360)throw std::runtime_error("network bridge map size");std::vector<UINT>map(rawmap.size());std::memcpy(map.data(),rawmap.data(),map.size()*4);bridge.Create(d,head.Output(),map,dir);source=bridge.Output();
  for(UINT i=0;i<8;i++){auto p=L"block"+std::to_wstring(31+i)+L"-";vit[i].Create(d,source,640,read(p+L"expand.f32"),read(p+L"contract.f32"),read(p+L"qkv.f32"),read(p+L"projection.f32"),dir);source=vit[i].Output();}
  decoder.Create(d,source,split[7].Output(),c256[7].Output(),c128[5].Output(),c64[3].Output(),c32[3].Output(),dir);
  post.Create(d,decoder.Output(),pre.Main(),rgb_hwc,1920,1152,read(L"post70-scales.f32"),read(L"post70-ffn.f32"),read(L"post70-attention.f32"),read(L"post70-head.f32"),dir,.03125f,post_shift);ready=true;
 }
 // Caller serializes whole frames and must retain this object after GPU timeout.
 // Input producer MUST already have been submitted to the same queue.
 // This includes temporal_rgb's sampler producer when temporal_enabled is true.
 // Binding storage does not imply a valid history frame; reset callers pass false.
 template<class Submission> void Run(Submission&submit,UINT seed,bool temporal_enabled=false){
  if(!ready||failed||!NativeSameDevice(submit.Device(),device))throw std::runtime_error("network unavailable/device mismatch");
  if(temporal_enabled&&!temporal_bound)throw std::runtime_error("network temporal RGB not bound");
  try{
   timestamps.Reset();
   submit.Submit([&](ID3D12GraphicsCommandList*c){
    timestamps.Mark(c,"start");pre.Record(c,seed,false,temporal_enabled);timestamps.Mark(c,"preblock");
    for(auto&s:c32)s.Record(c);ds4.Record(c);timestamps.Mark(c,"encoder1_4");
    for(auto&s:c64)s.Record(c);ds8.Record(c);timestamps.Mark(c,"encoder5_8");
    for(auto&s:c128)s.Record(c);ds14.Record(c);timestamps.Mark(c,"encoder9_14");
    for(auto&s:c256)s.Record(c);ds22.Record(c);timestamps.Mark(c,"encoder15_22");
    for(auto&s:split)s.Record(c);head.Record(c);bridge.Record(c);timestamps.Mark(c,"encoder23_head");
   });
   for(UINT b=0;b<8;b++){auto&layer=vit[b];for(UINT stage=0;stage<5;stage++)for(UINT chunk=0;chunk<layer.StageChunks(stage);chunk++)submit.Submit([&](ID3D12GraphicsCommandList*c){layer.RecordStageChunk(c,stage,chunk);if(chunk+1==layer.StageChunks(stage))timestamps.Mark(c,"vit"+std::to_string(31+b)+"_stage"+std::to_string(stage));});}
   for(UINT stage=0;stage<decoder.StageCount();stage++)submit.Submit([&](ID3D12GraphicsCommandList*c){decoder.RecordStage(c,stage);timestamps.Mark(c,"decoder_stage"+std::to_string(stage));});
   submit.Submit([&](ID3D12GraphicsCommandList*c){post.Record(c);timestamps.Mark(c,"post70");timestamps.Resolve(c);});
   if(profile)timestamps.Report(submit.TimestampFrequency());
  }catch(...){failed=true;throw;}
 }
 // For an already-recording external command list only. Does not close/reset,
 // submit, or wait. Caller owns ordering, GPU lifetime and TDR budgeting.
 // This batches the same stages as Run; single-list GPU safety is not implied
 // by the separately submitted/chunked tests and must be verified separately.
 void RecordUnsubmitted(ID3D12GraphicsCommandList*c,UINT seed,bool temporal_enabled=false){
  // Whole-network batching produced DXGI_ERROR_DEVICE_HUNG on9070XT.
  // Keep only as an explicit diagnostic; never silently enable in game code.
  const wchar_t*permit=_wgetenv(L"DLSS5_TEST_SINGLE_LIST");
  if(!permit||wcscmp(permit,L"1"))throw std::runtime_error("single-list network is diagnostic-only: observed device hang");
  if(!c)throw std::runtime_error("network null command list");
  ID3D12Device*owner=nullptr;auto hr=c->GetDevice(IID_PPV_ARGS(&owner));
  if(FAILED(hr))throw std::runtime_error("network command list device query");
  bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("network command list device mismatch");
  InlineRecorder recorder{device,c};Run(recorder,seed,temporal_enabled);
 }
 ID3D12Resource*Output()const{return post.Output();}
 ID3D12Resource*Head()const{return head.Output();}
 ID3D12Resource*Decoder69()const{return decoder.Output();}
private:
 struct InlineRecorder {
  ID3D12Device*device;ID3D12GraphicsCommandList*commands;
  ID3D12Device*Device()const{return device;}
  UINT64 TimestampFrequency()const{throw std::runtime_error("inline recorder cannot report completed timestamps");}
  template<class F>void Submit(F&&record){record(commands);}
 };
};
