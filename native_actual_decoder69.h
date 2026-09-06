#pragma once
#include "native_vit_gather.h"
#include "native_split_window.h"
#include "native_decoder_tail69.h"

// Captured 1920x1152 processing extent. All sources remain resident on the GPU.
class NativeActualDecoder69 {
 NativeVitGather inverse;
 NativeVitLinear entry,up48;
 NativeSplitWindow split[8];
 NativeC64Shift body48;
 NativeDecoderTail69 tail;
 bool created{};
public:
 void Create(ID3D12Device*d,ID3D12Resource*vit38,ID3D12Resource*skip30,
             ID3D12Resource*skip22,ID3D12Resource*skip14,ID3D12Resource*skip8,
             ID3D12Resource*skip4,const std::wstring&dir){
  if(created)throw std::runtime_error("actual decoder already created");
  auto read=[&](const std::wstring&name){
   std::ifstream f((dir+L"\\"+name).c_str(),std::ios::binary|std::ios::ate);
   if(!f)throw std::runtime_error("actual decoder coefficient missing");
   auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("actual decoder coefficient size");
   std::vector<float>v(size_t(n)/4);f.seekg(0);
   if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("actual decoder coefficient truncated");return v;
  };
  auto packed=read(L"vit-to-hwc.i32");std::vector<UINT>map(packed.size());
  std::memcpy(map.data(),packed.data(),packed.size()*4);
  if(map.size()!=655360)throw std::runtime_error("actual decoder inverse extent");
  inverse.Create(d,vit38,map,dir);
  entry.Create(d,inverse.Output(),skip30,640,1024,512,false,read(L"decoder39-weights.f32"),dir,true);
  auto*source=entry.Output();
  for(UINT i=0;i<8;i++){
   auto prefix=L"block"+std::to_wstring(40+i)+L"-";
   split[i].Create(d,source,60,36,NativeDecoderShift(40+i),read(prefix+L"ffwd.f32"),
                   read(prefix+L"ffwd-projection.f32"),read(prefix+L"attention.f32"),dir);
   source=split[i].Output();
  }
  up48.Create(d,source,skip22,2160,512,256,false,read(L"block48-weights.f32"),dir,true);
  body48.Create(d,up48.Output(),120,72,NativeDecoderShift(48),read(L"block48-ffn.f32"),
                read(L"block48-attention.f32"),dir,false,256);
  tail.Create(d,body48.Output(),skip14,skip8,skip4,dir,true);created=true;
 }
 // Callers may submit and fence between these stages without CPU feature copies.
 UINT StageCount()const{return 13;}
 void RecordStage(ID3D12GraphicsCommandList*c,UINT stage){
  if(!created||!c||stage>=StageCount())throw std::runtime_error("actual decoder stage");
  if(stage==0)inverse.Record(c);
  else if(stage==1)entry.Record(c);
  else if(stage<10)split[stage-2].Record(c);
  else if(stage==10)up48.Record(c);
  else if(stage==11)body48.Record(c);
  else tail.Record(c);
 }
 ID3D12Resource*Output()const{return tail.Output();}
};
