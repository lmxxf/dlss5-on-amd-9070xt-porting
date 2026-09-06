#pragma once
#include "native_preblock_runtime.h"
#include "native_shader_cache.h"
class NativeSplit {
 ID3D12Resource*input{};ID3D12Resource*weights[3]{};ID3D12Resource*result[4]{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[4]{};UINT geometry[2]{};bool recorded{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("C64 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE empty{};Check(r->Map(0,&empty,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);}return r;
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeSplit()=default;NativeSplit(const NativeSplit&)=delete;
 ~NativeSplit(){if(input)input->Release();for(auto*r:weights)if(r)r->Release();for(auto*r:result)if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,const std::vector<float>&fw,const std::vector<float>&fp,const std::vector<float>&aw,const std::wstring&dir,bool raw_output=false){
  if(input||!d||!src||!width||!height||width%8||height%8||fw.size()!=524288||fp.size()!=262656||aw.size()!=1114640)throw std::runtime_error("split contract");
  input=src;input->AddRef();geometry[0]=width;geometry[1]=height;
  weights[0]=Buffer(d,fw.size()*4,&fw);weights[1]=Buffer(d,fp.size()*4,&fp);weights[2]=Buffer(d,aw.size()*4,&aw);
  for(auto&r:result)r=Buffer(d,UINT64(width)*height*512*4);
  D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,2};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const char*entry[]={"ffwd","ffwd_projection","attention","projection"};D3D_SHADER_MACRO macros[]={{"RAW_OUTPUT","0"},{"CHANNELS","512"},{nullptr,nullptr}};
  for(UINT i=0;i<4;i++){macros[0].Definition=raw_output&&i==3?"1":"0";auto path=dir+(i<2?L"\\native_split.hlsl":L"\\native_c64.hlsl");blob=nullptr;error=nullptr;HRESULT hr=CompileNativeShader(path,macros,entry[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"split shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}

 }
 void Record(ID3D12GraphicsCommandList*c){
  if(recorded)for(auto*r:result)Barrier(c,r,true);
  for(UINT i=0;i<4;i++){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,(i?result[i-1]:input)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights[i<2?i:2]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,(i==3?result[1]:input)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[i]->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);c->Dispatch(geometry[0]*geometry[1]/64,i==2?16:1,1);Barrier(c,result[i],false);}recorded=true;
 }
 ID3D12Resource* Stage(UINT i)const{if(i>=4)throw std::runtime_error("split stage index");return result[i];}
 ID3D12Resource* Output()const{return result[3];}
};
