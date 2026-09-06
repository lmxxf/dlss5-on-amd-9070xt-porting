#pragma once
#include "native_split.h"
#include <algorithm>
class NativeVitGather {
 ID3D12Resource *input{},*indices{},*output{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};UINT count{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("ViT gather HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,bool upload){D3D12_HEAP_PROPERTIES hp{};hp.Type=upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=upload?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;}
 void barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeVitGather()=default;NativeVitGather(const NativeVitGather&)=delete;
 ~NativeVitGather(){for(auto*r:{input,indices,output})if(r)r->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,const std::vector<UINT>&map,const std::wstring&dir){
  if(input||!d||!src||(map.size()!=65536&&map.size()!=655360)||src->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||src->GetDesc().Width<map.size()*4)throw std::runtime_error("ViT gather geometry");auto sorted=map;std::sort(sorted.begin(),sorted.end());for(UINT i=0;i<sorted.size();i++)if(sorted[i]!=i)throw std::runtime_error("gather map must be a bounded bijection");
  input=src;input->AddRef();count=UINT(map.size());indices=buffer(d,UINT64(count)*4,true);output=buffer(d,UINT64(count)*4,false);void*p=nullptr;D3D12_RANGE empty{};ck(indices->Map(0,&empty,&p));std::memcpy(p,map.data(),map.size()*4);indices->Unmap(0,nullptr);
  D3D12_ROOT_PARAMETER params[4]{};params[0].ParameterType=params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].Descriptor.ShaderRegister=1;params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[3].Constants={0,0,1};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();blob=nullptr;error=nullptr;
  auto hr=CompileNativeShader(dir+L"\\native_vit_gather.hlsl",nullptr,"main",&blob,&error);if(FAILED(hr)){std::string message=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"gather compilation";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)barrier(c,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,indices->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,1,&count,0);c->Dispatch((count+63)/64,1,1);barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
};
