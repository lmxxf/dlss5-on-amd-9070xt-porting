#pragma once
#include "native_preblock_runtime.h"
#include "native_shader_cache.h"
#include "native_network_timestamps.h"
class NativeC64 {
 ID3D12Resource*input{};ID3D12Resource*weights[2]{};ID3D12Resource*result[3]{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[3]{};UINT geometry[2]{},channel_count{64};bool recorded{};
 ID3D12Resource*scratch[2]{};ID3D12PipelineState*split_pso[3]{};bool split_ffn{},tiled_contract{},tiled_expand{},tiled_projection{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("C64 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE empty{};Check(r->Map(0,&empty,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);}return r;
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeC64()=default;NativeC64(const NativeC64&)=delete;
 ~NativeC64(){if(input)input->Release();for(auto*r:weights)if(r)r->Release();for(auto*r:result)if(r)r->Release();for(auto*r:scratch)if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();for(auto*p:split_pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&dir,bool raw_output=false,UINT channels=64,bool fast_fp8=false,bool split=false,bool tile_contract=false,bool tile_expand=false,bool tile_projection=false){
  if(tile_contract&&(!split||UINT64(width)*height/8>65535))throw std::runtime_error("tiled contract geometry");tiled_contract=tile_contract;if(tile_expand&&!tile_contract)throw std::runtime_error("tiled expand requires tiled contract");tiled_expand=tile_expand;if(tile_projection&&!tile_expand)throw std::runtime_error("tiled projection requires tiled expand");tiled_projection=tile_projection;
  if(split&&(fast_fp8||UINT64(width)*height*4*channels>0xffffffffull))throw std::runtime_error("split FFN experiment contract");split_ffn=split;
  if(input||!d||!src||!width||!height||width%8||height%8||(channels!=64&&channels!=128&&channels!=256)||fw.size()!=9*channels*channels+channels||aw.size()!=4*channels*channels+(channels/32)*4096+channels/32+channels)throw std::runtime_error("multihead contract");input=src;input->AddRef();geometry[0]=width;geometry[1]=height;channel_count=channels;
  weights[0]=Buffer(d,fw.size()*4,&fw);weights[1]=Buffer(d,aw.size()*4,&aw);for(auto&r:result)r=Buffer(d,UINT64(width)*height*channels*4);
  D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,2};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const char*entry[]={"ffn","attention",tiled_projection?"tiled_attention_project":"projection"};auto path=dir+L"\\native_c64.hlsl";auto channel_text=std::to_string(channels);D3D_SHADER_MACRO macros[]={{"RAW_OUTPUT",raw_output?"1":"0"},{"CHANNELS",channel_text.c_str()},{"NATIVE_FAST_FP8",fast_fp8?"1":"0"},{nullptr,nullptr}};
  for(UINT i=0;i<3;i++){if(split_ffn&&i==0)continue;macros[0].Definition=(raw_output&&i==2)?"1":"0";blob=nullptr;error=nullptr;HRESULT hr=CompileNativeShader(path,macros,entry[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"C64 shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}
  if(split_ffn){
   scratch[0]=Buffer(d,UINT64(width)*height*channels*16);scratch[1]=Buffer(d,UINT64(width)*height*channels*4);
   const char*names[]={tiled_expand?"tiled_ffn_expand":"split_ffn_expand",tiled_contract?"tiled_ffn_contract":"split_ffn_contract",tiled_projection?"tiled_ffn_project":"split_ffn_project"};macros[0].Definition="0";
   for(UINT i=0;i<3;i++){blob=nullptr;error=nullptr;auto hr=CompileNativeShader(path,macros,names[i],&blob,&error);if(error)error->Release();Check(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&split_pso[i]));blob->Release();Check(hr);}
  }
 }
 void Record(ID3D12GraphicsCommandList*c,NativeNetworkTimestamps*timer=nullptr,const char*label="core"){
  if(recorded)for(auto*r:result)Barrier(c,r,true);
  if(split_ffn){
   if(recorded)for(auto*r:scratch)Barrier(c,r,true);
   for(UINT i=0;i<3;i++){auto*src=i?scratch[i-1]:input;auto*dst=i==2?result[0]:scratch[i];c->SetComputeRootSignature(root);c->SetPipelineState(split_pso[i]);c->SetComputeRootShaderResourceView(0,src->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights[0]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,dst->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);UINT64 groups=(UINT64(geometry[0])*geometry[1]*channel_count*(i==0?4:1)+63)/64;if(i==0&&tiled_expand)c->Dispatch(channel_count*4/32,geometry[0]*geometry[1]/8,1);else if((i==1&&tiled_contract)||(i==2&&tiled_projection))c->Dispatch(channel_count/32,geometry[0]*geometry[1]/8,1);else c->Dispatch(UINT(std::min<UINT64>(groups,65535)),UINT((groups+65534)/65535),1);Barrier(c,dst,false);if(timer)timer->Mark(c,std::string(label)+"_ffn_part"+std::to_string(i));}
  }
  for(UINT i=split_ffn?1:0;i<3;i++){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,(i?result[i-1]:input)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights[i?1:0]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,(i==2?result[0]:input)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[i]->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,2,geometry,0);if(i==2&&tiled_projection)c->Dispatch(channel_count/32,geometry[0]*geometry[1]/8,1);else c->Dispatch(geometry[0]*geometry[1]/64,i==1?channel_count/32:1,1);Barrier(c,result[i],false);if(timer)timer->Mark(c,std::string(label)+(i==0?"_ffn":i==1?"_attention":"_projection"));}recorded=true;
 }
 ID3D12Resource* Output()const{return result[2];}
};
