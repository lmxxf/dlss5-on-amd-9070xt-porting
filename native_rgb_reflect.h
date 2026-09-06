#pragma once
#include "native_split.h"
// GPU HWC float4 -> reflected8x8 tile-major float4. No neural features injected.
class NativeRgbReflect {
 ID3D12Resource *input{},*output{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT geometry[4]{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("RGB reflect HRESULT="+std::to_string(unsigned(h)));}
 void barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeRgbReflect()=default;NativeRgbReflect(const NativeRgbReflect&)=delete;
 ~NativeRgbReflect(){if(input)input->Release();if(output)output->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT valid_w,UINT valid_h,UINT w,UINT h,const std::wstring&dir){
  if(input||!d||!src||valid_w<2||valid_h<2||valid_w>w||valid_h>h||w>16384||h>16384||w%8||h%8||src->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||src->GetDesc().Width<UINT64(valid_w)*valid_h*16)throw std::runtime_error("RGB reflect geometry/capacity");
  input=src;input->AddRef();geometry[0]=valid_w;geometry[1]=valid_h;geometry[2]=w;geometry[3]=h;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(w)*h*16;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,4};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;ID3DBlob*blob=nullptr,*error=nullptr;
  auto hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);if(error)error->Release();ck(hr);ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();blob=nullptr;error=nullptr;
  hr=CompileNativeShader(dir+L"\\native_rgb_reflect.hlsl",nullptr,"main",&blob,&error);
  if(FAILED(hr)){std::string msg=error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"RGB reflect compile";if(error)error->Release();throw std::runtime_error(msg);}if(error)error->Release();
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso));blob->Release();ck(hr);
 }
 void Record(ID3D12GraphicsCommandList*c){if(!output||!c)throw std::runtime_error("RGB reflect not created");if(recorded)barrier(c,true);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,4,geometry,0);c->Dispatch(geometry[2]/8,geometry[3]/8,1);barrier(c,false);recorded=true;}
 ID3D12Resource*Output()const{return output;}
};
