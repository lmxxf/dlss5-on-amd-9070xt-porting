#pragma once
#include "native_rgb_reflect.h"
// GPU history + already-transformed pixel-center coordinates -> reconstructed
// HWC RGB. Coordinate generation/history lifetime remain caller responsibilities.
class NativeTemporalSample {
 ID3D12Resource *history{},*coordinates{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT geometry[3]{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("temporal sampler HRESULT="+std::to_string(unsigned(h)));}
 void transition(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeTemporalSample()=default;NativeTemporalSample(const NativeTemporalSample&)=delete;
 ~NativeTemporalSample(){for(auto*r:{history,coordinates,output})if(r)r->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*source,ID3D12Resource*xy,UINT width,UINT height,UINT count,const std::wstring&dir){
  if(history||!d||!source||!xy||!width||!height||!count||width>16384||height>16384||count>65535u*64)throw std::runtime_error("temporal sampler geometry");
  for(auto item:{std::pair<ID3D12Resource*,UINT64>{source,UINT64(width)*height*16},{xy,UINT64(count)*8}}){
   if(item.first->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||item.first->GetDesc().Width<item.second)throw std::runtime_error("temporal sampler capacity");
   ID3D12Device*owner=nullptr;ck(item.first->GetDevice(IID_PPV_ARGS(&owner)));bool same=owner==d;owner->Release();if(!same)throw std::runtime_error("temporal sampler device mismatch");
  }
  history=source;history->AddRef();coordinates=xy;coordinates->AddRef();geometry[0]=width;geometry[1]=height;geometry[2]=count;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(count)*16;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_ROOT_PARAMETER p[4]{};p[0].ParameterType=p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].Descriptor.ShaderRegister=1;p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[3].Constants={0,0,3};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=p;
  ID3DBlob*code=nullptr,*error=nullptr;auto hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error);if(error)error->Release();ck(hr);ck(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root)));code->Release();code=nullptr;error=nullptr;
  hr=CompileNativeShader(dir+L"\\native_temporal_sample.hlsl",nullptr,"main",&code,&error);if(error)error->Release();ck(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));code->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){
  if(!pso||!c)throw std::runtime_error("temporal sampler not created");if(recorded)transition(c,true);
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,history->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,coordinates->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,3,geometry,0);c->Dispatch((geometry[2]+63)/64,1,1);transition(c,false);recorded=true;
 }
 ID3D12Resource*Output()const{return output;}
};
