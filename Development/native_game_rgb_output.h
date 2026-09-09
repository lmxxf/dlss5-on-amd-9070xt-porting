#pragma once
#include "native_rgb_reflect.h"

// Copies valid RGB rows to a same-device R10 texture. Caller owns synchronization
// and color-space validation; no game command list is closed/submitted here.
class NativeGameRgbOutput {
 ID3D12Device*device{};ID3D12Resource *input{},*packed{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("game output HRESULT="+std::to_string(unsigned(h)));}
 static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&t);}
public:
 NativeGameRgbOutput()=default;NativeGameRgbOutput(const NativeGameRgbOutput&)=delete;
 ~NativeGameRgbOutput(){if(device)device->Release();if(input)input->Release();if(packed)packed->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*rgb,const std::wstring&dir){
  if(device||!d||!rgb||rgb->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||rgb->GetDesc().Width<1920ull*1152*3*4)throw std::runtime_error("game output input contract");
  ID3D12Device*owner=nullptr;ck(rgb->GetDevice(IID_PPV_ARGS(&owner)));bool same=owner==d;owner->Release();if(!same)throw std::runtime_error("game output input device mismatch");
  device=d;device->AddRef();input=rgb;input->AddRef();
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=1920ull*1080*4;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&packed)));
  D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=p;
  ID3DBlob*blob=nullptr,*error=nullptr;auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);if(error)error->Release();ck(hr);ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();blob=nullptr;error=nullptr;
  hr=CompileNativeShader(dir+L"\\native_game_rgb_output.hlsl",nullptr,"main",&blob,&error);if(error)error->Release();ck(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 // Input must be SRV-readable. Destination is restored to the caller's state.
 void Record(ID3D12GraphicsCommandList*c,ID3D12Resource*target,D3D12_RESOURCE_STATES before){
  if(!pso||!c||!target)throw std::runtime_error("game output not initialized");auto td=target->GetDesc();
  if(td.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||td.Width!=1920||td.Height!=1080||td.DepthOrArraySize!=1||td.MipLevels!=1||td.SampleDesc.Count!=1||td.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)throw std::runtime_error("unverified display texture");
  ID3D12Device*owner=nullptr;ck(target->GetDevice(IID_PPV_ARGS(&owner)));bool same=owner==device;owner->Release();if(!same)throw std::runtime_error("display device mismatch");
  if(recorded)transition(c,packed,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,packed->GetGPUVirtualAddress());c->Dispatch(32400,1,1);
  transition(c,packed,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(c,target,before,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=packed;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={DXGI_FORMAT_R10G10B10A2_UNORM,1920,1080,1,7680};
  D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=target;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  transition(c,target,D3D12_RESOURCE_STATE_COPY_DEST,before);recorded=true;
 }
};
