#pragma once
#include "native_split.h"
class NativeVitAttention {
 ID3D12Resource *input{},*output{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};UINT count{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("ViT attention HRESULT="+std::to_string(unsigned(h)));}
 void barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeVitAttention()=default;NativeVitAttention(const NativeVitAttention&)=delete;
 ~NativeVitAttention(){if(input)input->Release();if(output)output->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT tokens,const std::wstring&dir){
  if(input||!d||!src||(tokens!=64&&tokens!=128&&tokens!=256))throw std::runtime_error("ViT attention supports 64/128/256 tokens");input=src;input->AddRef();count=tokens;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(tokens)*1024*4;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_ROOT_PARAMETER params[3]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,1};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();blob=nullptr;error=nullptr;
  auto hr=CompileNativeShader(dir+L"\\native_vit_attention.hlsl",nullptr,"main",&blob,&error);if(FAILED(hr)){std::string message=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"attention compilation";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)barrier(c,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,1,&count,0);c->Dispatch((count*32+63)/64,1,1);barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
};
