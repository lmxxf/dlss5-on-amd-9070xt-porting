#pragma once
#include <fstream>
#include "native_vit_linear.h"
#include "native_c64_shift.h"
#include "native_c32_stage.h"
// Controlled RGB512 geometry only; weights are loaded once, never in Record.
class NativeDecoderTail69 {
 NativeC64Shift c256[7],c128[5],c64[3],body56,body62;
 NativeC32Stage body66,c32[3];
 NativeVitLinear project56,project62,project66;
 ID3D12Resource*output{};
public:
 NativeDecoderTail69()=default;NativeDecoderTail69(const NativeDecoderTail69&)=delete;
 void Create(ID3D12Device*d,ID3D12Resource*input48,ID3D12Resource*skip14,ID3D12Resource*skip8,ID3D12Resource*skip4,const std::wstring&dir){
  if(output||!d||!input48||!skip14||!skip8||!skip4)throw std::runtime_error("decoder tail contract");
  auto read=[&](UINT block,const wchar_t*name){
   auto path=dir+L"\\block"+std::to_wstring(block)+L"-"+name+L".f32";
   std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);
   if(!f)throw std::runtime_error("missing decoder tail coefficient");auto bytes=f.tellg();
   if(bytes<=0||size_t(bytes)%4)throw std::runtime_error("decoder coefficient size");
   std::vector<float>v(size_t(bytes)/4);f.seekg(0);if(!f.read((char*)v.data(),bytes))throw std::runtime_error("short decoder coefficient");return v;
  };
  const UINT shifts[]={0,1,3,2,0,1,3};auto*source=input48;
  for(UINT i=0;i<7;i++){c256[i].Create(d,source,32,32,shifts[i],read(49+i,L"ffn"),read(49+i,L"attention"),dir,false,256);source=c256[i].Output();}
  project56.Create(d,source,skip14,1024,256,128,false,read(56,L"weights"),dir,true);
  body56.Create(d,project56.Output(),64,64,0,read(56,L"ffn"),read(56,L"attention"),dir,false,128);source=body56.Output();
  for(UINT i=0;i<5;i++){c128[i].Create(d,source,64,64,shifts[i],read(57+i,L"ffn"),read(57+i,L"attention"),dir,false,128);source=c128[i].Output();}
  project62.Create(d,source,skip8,4096,128,64,false,read(62,L"weights"),dir,true);
  body62.Create(d,project62.Output(),128,128,0,read(62,L"ffn"),read(62,L"attention"),dir,false,64);source=body62.Output();
  for(UINT i=0;i<3;i++){c64[i].Create(d,source,128,128,shifts[i],read(63+i,L"ffn"),read(63+i,L"attention"),dir,false,64);source=c64[i].Output();}
  project66.Create(d,source,skip4,16384,64,32,false,read(66,L"weights"),dir,true);
  body66.Create(d,project66.Output(),256,256,0,read(66,L"ffn"),read(66,L"attention"),dir);source=body66.Output();
  for(UINT i=0;i<3;i++){c32[i].Create(d,source,256,256,shifts[i],read(67+i,L"ffn"),read(67+i,L"attention"),dir);source=c32[i].Output();}
  output=source;
 }
 void Record(ID3D12GraphicsCommandList*c){
  if(!output)throw std::runtime_error("decoder tail not created");
  for(auto&layer:c256)layer.Record(c);project56.Record(c);body56.Record(c);
  for(auto&layer:c128)layer.Record(c);project62.Record(c);body62.Record(c);
  for(auto&layer:c64)layer.Record(c);project66.Record(c);body66.Record(c);
  for(auto&layer:c32)layer.Record(c);
 }
 ID3D12Resource*Output()const{return output;}
};
