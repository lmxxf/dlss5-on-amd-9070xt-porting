#pragma once
#include "native_preblock_runtime.h"
// Consumes the raw-before-quantization half-pool produced by NativeC32Stage.
class NativeC32Downsample {
 ID3D12Resource *input{},*weights{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT geometry[5]{};bool recorded{};
 static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("C32 DS HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,bool upload){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=upload?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;
 }
 void Barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeC32Downsample()=default;NativeC32Downsample(const NativeC32Downsample&)=delete;
 ~NativeC32Downsample(){if(input)input->Release();if(weights)weights->Release();if(output)output->Release();if(root)root->Release();if(pso)pso->Release();}
 // Raw path pools already-cropped half-valued HWC, then projects C -> 2C.
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,UINT shift,const std::vector<float>&w,const std::wstring&dir,bool c64_raw=false,UINT channels=64){
  if(channels==512&&(width<8||height<8||width%8||height%8))throw std::runtime_error("unverified split pool/head extent");
  if(input||!d||!src||(channels!=64&&channels!=128&&channels!=256&&channels!=512)||(!c64_raw&&channels!=64)||w.size()!=(c64_raw?2*channels*channels:2048)||!width||!height||(c64_raw?(width%2||height%2):(width%8||height%8))||shift>3||(c64_raw&&shift))throw std::runtime_error("DS contract");
  input=src;input->AddRef();geometry[0]=width/2;geometry[1]=height/2;geometry[3]=(shift&1)?2:0;geometry[4]=(shift&2)?2:0;geometry[2]=width/2+geometry[3]*2;
  if(c64_raw)geometry[2]=width;
  output=Buffer(d,UINT64(width/2)*(height/2)*(c64_raw?2*channels:64)*4,false);weights=Buffer(d,w.size()*4,true);void*ptr=nullptr;D3D12_RANGE empty{};Check(weights->Map(0,&empty,&ptr));std::memcpy(ptr,w.data(),w.size()*4);weights->Unmap(0,nullptr);
  D3D12_ROOT_PARAMETER parameters[4]{};parameters[0].ParameterType=parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[1].Descriptor.ShaderRegister=1;parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[3].Constants={0,0,5};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=parameters;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();error=nullptr;
  auto channel_text=std::to_string(channels);D3D_SHADER_MACRO macros[]={{"CHANNELS",channel_text.c_str()},{nullptr,nullptr}};
  auto path=dir+(c64_raw?L"\\native_c64_ds.hlsl":L"\\native_c32_ds.hlsl");auto hr=D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
  if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"DS shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)Barrier(c,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,5,geometry,0);c->Dispatch((geometry[0]*geometry[1]+63)/64,1,1);Barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
};
