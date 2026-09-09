#pragma once
#include "native_block_skip.h"
#include "native_vram_log.h"
#include <fstream>
#include "native_vit_linear.h"
#include "native_c64_shift.h"
#include "native_c32_stage.h"
#include "native_runtime_shifts.h"
// Controlled RGB512 and captured1920x1152 geometries; weights are loaded once, never in Record.
class NativeDecoderTail69 {
 NativeC64Shift c256[7],c128[5],c64[3],body56,body62;
 NativeC32Stage body66,c32[3];
 NativeVitLinear project56,project62,project66;
 ID3D12Resource*output{};
public:
 NativeDecoderTail69()=default;NativeDecoderTail69(const NativeDecoderTail69&)=delete;
 void Create(ID3D12Device*d,ID3D12Resource*input48,ID3D12Resource*skip14,ID3D12Resource*skip8,ID3D12Resource*skip4,const std::wstring&dir,bool game_extent=false,NativeMatrixWorkspace*workspace=nullptr){
  if(output||!d||!input48||!skip14||!skip8||!skip4)throw std::runtime_error("decoder tail contract");
  const UINT w=game_extent?120:32,h=game_extent?72:32;
  auto capacity=[&](ID3D12Resource*r,UINT64 values){if(r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<values*4)throw std::runtime_error("decoder tail buffer capacity");};
  capacity(input48,UINT64(w)*h*256);capacity(skip14,UINT64(w)*h*4*128);capacity(skip8,UINT64(w)*h*16*64);capacity(skip4,UINT64(w)*h*64*32);
  auto read=[&](UINT block,const wchar_t*name){
   auto path=dir+L"\\block"+std::to_wstring(block)+L"-"+name+L".f32";
   std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);
   if(!f)throw std::runtime_error("missing decoder tail coefficient");auto bytes=f.tellg();
   if(bytes<=0||size_t(bytes)%4)throw std::runtime_error("decoder coefficient size");
   std::vector<float>v(size_t(bytes)/4);f.seekg(0);if(!f.read((char*)v.data(),bytes))throw std::runtime_error("short decoder coefficient");return v;
  };
  auto*source=input48;
  for(UINT i=0;i<7;i++){c256[i].Create(d,source,w,h,NativeDecoderShift(49+i),read(49+i,L"ffn"),read(49+i,L"attention"),dir,false,256,false,workspace,i>0,i<6);source=c256[i].Output();}
  NativeVramLog(d,"tail c256x7");project56.Create(d,source,skip14,w*h,256,128,false,read(56,L"weights"),dir,true);
  body56.Create(d,project56.Output(),w*2,h*2,NativeDecoderShift(56),read(56,L"ffn"),read(56,L"attention"),dir,false,128,false,workspace,false,true);source=body56.Output();
  for(UINT i=0;i<5;i++){c128[i].Create(d,source,w*2,h*2,NativeDecoderShift(57+i),read(57+i,L"ffn"),read(57+i,L"attention"),dir,false,128,false,workspace,true,i<4);source=c128[i].Output();}
  NativeVramLog(d,"tail 56+c128x5");project62.Create(d,source,skip8,w*h*4,128,64,false,read(62,L"weights"),dir,true);
  body62.Create(d,project62.Output(),w*4,h*4,0,read(62,L"ffn"),read(62,L"attention"),dir,false,64,false,workspace,false,true);source=body62.Output();
  for(UINT i=0;i<3;i++){c64[i].Create(d,source,w*4,h*4,NativeDecoderShift(63+i),read(63+i,L"ffn"),read(63+i,L"attention"),dir,false,64,false,workspace,true,i<2);source=c64[i].Output();}
  NativeVramLog(d,"tail 62+c64x3");project66.Create(d,source,skip4,w*h*16,64,32,false,read(66,L"weights"),dir,true);
  const wchar_t*c32_mapped=_wgetenv(L"DLSS5_C32_MAPPED_INPUT");const bool chain=c32_mapped&&!wcscmp(c32_mapped,L"1");
  const wchar_t*cr=_wgetenv(L"DLSS5_C32_CHAIN_RAW");if(cr&&wcscmp(cr,L"0")&&wcscmp(cr,L"1"))throw std::runtime_error("invalid C32 chain raw flag");const bool chain_raw=chain&&cr&&!wcscmp(cr,L"1");
  for(UINT i=0;i<4;i++){NativeC32Stage&stage=i?c32[i-1]:body66;const NativeC32Stage*prev=i>1?&c32[i-2]:i==1?&body66:nullptr;
   if(i)stage.Create(d,source,w*8,h*8,NativeDecoderShift(66+i),read(66+i,L"ffn"),read(66+i,L"attention"),dir,false,chain);else stage.Create(d,project66.Output(),w*8,h*8,0,read(66,L"ffn"),read(66,L"attention"),dir,false,chain);
   if(chain){if(prev){if(chain_raw){stage.ChainFromRaw(*prev);const_cast<NativeC32Stage*>(prev)->SetSkipFinish(true);}else stage.ChainFrom(*prev);const_cast<NativeC32Stage*>(prev)->SetCropNeeded(false);}else stage.MapFromRaster(project66.Output());}
   source=(chain_raw&&i<3)?stage.RawWork():stage.Output();}
  NativeVramLog(d,"tail 66+c32x3");output=source;
 }
 void Record(ID3D12GraphicsCommandList*c,NativeNetworkTimestamps*timer=nullptr){
  if(!output)throw std::runtime_error("decoder tail not created");
  auto mark=[&](const char*name){if(timer)timer->Mark(c,name);};
  auto run=[&](auto&layer,UINT block){if(NativeSkipBlock(block))NativeSkipCopy(c,layer.Input(),layer.Output(),block);else layer.Record(c);};
  for(UINT i=0;i<7;i++)run(c256[i],49+i);mark("tail49_55");project56.Record(c);mark("tail56_project");body56.Record(c);mark("tail56_body");
  for(UINT i=0;i<5;i++)run(c128[i],57+i);mark("tail57_61");project62.Record(c);mark("tail62_project");body62.Record(c);mark("tail62_body");
  for(UINT i=0;i<3;i++)run(c64[i],63+i);mark("tail63_65");project66.Record(c);mark("tail66_project");body66.Record(c);mark("tail66_body");
  for(auto&layer:c32)layer.Record(c);mark("tail67_69");
 }
 ID3D12Resource*Output()const{return output;}
};
