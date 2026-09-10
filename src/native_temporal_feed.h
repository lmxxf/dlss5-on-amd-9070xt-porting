#pragma once
#include "native_lab_paths.h"
#include "native_pinned_resource.h"
#include "native_split.h"
// Motion-vector texture -> motion buffer, and network output -> history buffer.
class NativeTemporalFeed {
 ID3D12Resource*motion{},*history{},*rgb{},*bound_texture{};ID3D12DescriptorHeap*heap{};
 ID3D12RootSignature*motion_root{},*history_root{};ID3D12PipelineState*motion_pso{},*history_pso{};
 UINT mw{},mh{};float scale[2]{};bool motion_recorded{},history_recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("temporal feed HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource*Buffer(ID3D12Device*d,UINT64 bytes){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;ck(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;}
 static void Transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool to_uav){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,to_uav?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,to_uav?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeTemporalFeed()=default;NativeTemporalFeed(const NativeTemporalFeed&)=delete;
 ~NativeTemporalFeed(){for(auto*r:{motion,history,rgb,bound_texture})if(r)r->Release();if(heap)heap->Release();for(auto*r:{motion_root,history_root})if(r)r->Release();for(auto*p:{motion_pso,history_pso})if(p)p->Release();}
 // motion_w/h: motion texture size; pixel scale: 1080p pixels per motion unit (upscale size for UV-unit vectors).
 void Create(ID3D12Device*d,UINT motion_w,UINT motion_h,float pixel_scale_x,float pixel_scale_y,const std::wstring&dir){
  if(motion||!d||!motion_w||!motion_h)throw std::runtime_error("temporal feed contract");
  mw=motion_w;mh=motion_h;scale[0]=pixel_scale_x;scale[1]=pixel_scale_y;
  motion=Buffer(d,UINT64(mw)*mh*16);history=Buffer(d,1920ull*1080*16);
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ck(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
  {D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,4};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;ID3DBlob*b=nullptr,*e=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&b,&e));ck(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&motion_root)));b->Release();if(e)e->Release();}
  {D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,4};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;ID3DBlob*b=nullptr,*e=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&b,&e));ck(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&history_root)));b->Release();if(e)e->Release();}
  for(UINT i=0;i<2;i++){D3D_SHADER_MACRO macros[]={{"FEED_MOTION",i?"0":"1"},{nullptr,nullptr}};ID3DBlob*code=nullptr,*error=nullptr;auto hr=CompileNativeShader(dir+L"\\native_temporal_feed.hlsl",macros,i?"history_main":"motion_main",&code,&error);if(FAILED(hr)){std::string m=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"temporal feed compile";if(error)error->Release();throw std::runtime_error(m);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=i?history_root:motion_root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(i?&history_pso:&motion_pso)));code->Release();}
 }
 void BindNetworkOutput(ID3D12Resource*network_rgb){if(rgb)rgb->Release();rgb=network_rgb;rgb->AddRef();}
 // Texture must be readable as a non-pixel shader resource when recorded (FFX compute-read state).
 void RecordMotion(ID3D12GraphicsCommandList*c,ID3D12Resource*texture){
  if(!texture)throw std::runtime_error("motion texture missing");
  if(texture!=bound_texture){
   auto desc=texture->GetDesc();if(desc.Width!=mw||desc.Height!=mh)throw std::runtime_error("motion texture size changed");
   ID3D12Device*d=nullptr;ck(heap->GetDevice(IID_PPV_ARGS(&d)));D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=NativeViewFormat(desc.Format);sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;d->CreateShaderResourceView(texture,&sv,heap->GetCPUDescriptorHandleForHeapStart());d->Release();
   texture->AddRef();if(bound_texture)bound_texture->Release();bound_texture=texture;
  }
  if(motion_recorded)Transition(c,motion,true);
  UINT words[4]={mw,mh,0,0};std::memcpy(words+2,scale,8);
  c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(motion_root);c->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());c->SetComputeRootUnorderedAccessView(1,motion->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,4,words,0);c->SetPipelineState(motion_pso);c->Dispatch((mw+15)/16,(mh+15)/16,1);
  Transition(c,motion,false);motion_recorded=true;
 }
 void RecordHistory(ID3D12GraphicsCommandList*c){
  if(!rgb)throw std::runtime_error("history source not bound");
  if(history_recorded)Transition(c,history,true);
  UINT words[4]={1920,1080,0,0};
  c->SetComputeRootSignature(history_root);c->SetComputeRootShaderResourceView(0,rgb->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,history->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,4,words,0);c->SetPipelineState(history_pso);c->Dispatch(120,68,1);
  Transition(c,history,false);history_recorded=true;
 }
 ID3D12Resource*Motion()const{return motion;}ID3D12Resource*History()const{return history;}
 bool HasHistory()const{return history_recorded;}
};
