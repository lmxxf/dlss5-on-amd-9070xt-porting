#pragma once
#include "native_rgb_reflect.h"
#include <cmath>
// No slot18 path. Output is HWC float2; callers supply captured motion transform.
class NativeTemporalCoordinates {
 ID3D12Resource *motion{},*output{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT constants[12]{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("motion coordinates HRESULT="+std::to_string(unsigned(h)));}
 void transition(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeTemporalCoordinates()=default;NativeTemporalCoordinates(const NativeTemporalCoordinates&)=delete;
 ~NativeTemporalCoordinates(){if(motion)motion->Release();if(output)output->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT vw,UINT vh,UINT pw,UINT ph,
             UINT mw,UINT mh,const float(&transform)[6],const std::wstring&dir){
  if(motion||!d||!src||vw<2||vh<2||mw<2||mh<2||pw<vw||ph<vh||vw>16384||vh>16384||mw>16384||mh>16384||pw>2*vw-2||ph>2*vh-2||UINT64(pw)*ph>65535ull*64)throw std::runtime_error("motion coordinate geometry");
  for(float v:transform)if(!std::isfinite(v))throw std::runtime_error("nonfinite motion transform");
  if(transform[2]<=0||transform[3]<=0)throw std::runtime_error("motion subrect extent");
  if(src->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||src->GetDesc().Width<UINT64(mw)*mh*16)throw std::runtime_error("motion buffer capacity");
  ID3D12Device*owner=nullptr;ck(src->GetDevice(IID_PPV_ARGS(&owner)));bool same=owner==d;owner->Release();if(!same)throw std::runtime_error("motion buffer device mismatch");
  motion=src;motion->AddRef();UINT dims[]={vw,vh,pw,ph,mw,mh};std::memcpy(constants,dims,sizeof(dims));std::memcpy(constants+6,transform,sizeof(transform));
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=UINT64(pw)*ph*8;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,12};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=p;
  ID3DBlob*code=nullptr,*error=nullptr;auto hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error);if(error)error->Release();ck(hr);ck(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root)));code->Release();code=nullptr;error=nullptr;
  hr=CompileNativeShader(dir+L"\\native_temporal_coordinates.hlsl",nullptr,"main",&code,&error);if(error)error->Release();ck(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));code->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){
  if(!pso||!c)throw std::runtime_error("motion coordinate pass unavailable");if(recorded)transition(c,true);
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,motion->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,12,constants,0);c->Dispatch((constants[2]*constants[3]+63)/64,1,1);transition(c,false);recorded=true;
 }
 ID3D12Resource*Output()const{return output;}
};
