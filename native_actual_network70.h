#pragma once
#include "native_c32_ds.h"
#include "native_vit_block.h"
#include "native_actual_decoder69.h"
#include "native_post70.h"
#include "native_game_submission.h"

// Actual processing extent, with externally supplied GPU RGB tiles and HWC base.
// No fixture activations, oracles, game command lists, or CPU pixel readbacks.
class NativeActualNetwork70 {
 NativePreblockRuntime pre;
 NativeC32Stage c32[4];NativeC64Shift c64[4],c128[6],c256[8];
 NativeC32Downsample ds4,ds8,ds14,ds22,head;
 NativeSplitWindow split[8];NativeVitGather bridge;NativeVitBlock vit[8];
 NativeActualDecoder69 decoder;NativePost70 post;
 ID3D12Device*device{};bool ready{},failed{};
 static std::vector<float>Read(const std::wstring&path){
  std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("network coefficient missing");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("network coefficient size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("network coefficient truncated");return v;
 }
public:
 NativeActualNetwork70()=default;NativeActualNetwork70(const NativeActualNetwork70&)=delete;
 ~NativeActualNetwork70(){if(device)device->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*rgb_tiles,ID3D12Resource*rgb_hwc,
             const std::vector<float>&noise,const std::wstring&dir){
  if(device||!d||!rgb_tiles||!rgb_hwc||noise.size()!=201326592/4)throw std::runtime_error("network initialization contract");
  if(_wgetenv(L"DLSS5_POST_BASE_ONLY"))throw std::runtime_error("diagnostic post forbidden");
  for(auto*r:{rgb_tiles,rgb_hwc}){
   if(r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<1920ull*1152*16)throw std::runtime_error("network RGB capacity");
   ID3D12Device*owner=nullptr;auto hr=r->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr))throw std::runtime_error("network RGB device query");bool same=owner==d;owner->Release();if(!same)throw std::runtime_error("network RGB device mismatch");
  }
  device=d;device->AddRef();auto read=[&](const std::wstring&name){return Read(dir+L"\\"+name);};
  pre.Create(d,rgb_tiles,1920,1152,read(L"block0-ffn.f32"),read(L"block0-attention.f32"),dir,true,false,&noise);
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
  post.Create(d,decoder.Output(),pre.Main(),rgb_hwc,1920,1152,read(L"post70-scales.f32"),read(L"post70-ffn.f32"),read(L"post70-attention.f32"),read(L"post70-head.f32"),dir);ready=true;
 }
 // Caller serializes whole frames and must retain this object after GPU timeout.
 // Input producer MUST already have been submitted to the same queue.
 void Run(NativeGameSubmission&submit,UINT seed){
  if(!ready||failed||submit.Device()!=device)throw std::runtime_error("network unavailable/device mismatch");
  try{
   submit.Submit([&](ID3D12GraphicsCommandList*c){pre.Record(c,seed,false);for(auto&s:c32)s.Record(c);ds4.Record(c);for(auto&s:c64)s.Record(c);ds8.Record(c);for(auto&s:c128)s.Record(c);ds14.Record(c);for(auto&s:c256)s.Record(c);ds22.Record(c);for(auto&s:split)s.Record(c);head.Record(c);bridge.Record(c);});
   for(auto&layer:vit)for(UINT stage=0;stage<5;stage++)for(UINT chunk=0;chunk<layer.StageChunks(stage);chunk++)submit.Submit([&](ID3D12GraphicsCommandList*c){layer.RecordStageChunk(c,stage,chunk);});
   for(UINT stage=0;stage<decoder.StageCount();stage++)submit.Submit([&](ID3D12GraphicsCommandList*c){decoder.RecordStage(c,stage);});
   submit.Submit([&](ID3D12GraphicsCommandList*c){post.Record(c);});
  }catch(...){failed=true;throw;}
 }
 ID3D12Resource*Output()const{return post.Output();}
 ID3D12Resource*Head()const{return head.Output();}
 ID3D12Resource*Decoder69()const{return decoder.Output();}
};
