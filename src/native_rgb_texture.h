#pragma once
#include "native_device_identity.h"
#include "native_game_codec.h"
// Caller submits the network producer first on the same queue and retains all
// resources through completion. No readback or fixture data in this stage.
class NativeRgbTexture {
 ID3D12Resource*input{},*output{};ID3D12DescriptorHeap*heap{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};bool recorded{};
 static void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("RGB texture HRESULT="+std::to_string(unsigned(hr)));}
 static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&t);}
public:
 NativeRgbTexture()=default;NativeRgbTexture(const NativeRgbTexture&)=delete;
 ~NativeRgbTexture(){if(input)input->Release();if(output)output->Release();if(heap)heap->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*rgb,const std::wstring&dir){
  if(input||!d||!rgb)throw std::runtime_error("RGB texture initialization");auto desc=rgb->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||desc.Width<1920ull*1152*12)throw std::runtime_error("RGB texture input capacity");
  ID3D12Device*owner=nullptr;check(rgb->GetDevice(IID_PPV_ARGS(&owner)));bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("RGB texture device mismatch");input=rgb;input->AddRef();
  D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1920;td.Height=1080;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=td.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(output,nullptr,&uv,heap->GetCPUDescriptorHandleForHeapStart());
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[0].Descriptor.ShaderRegister=0;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={1,&range};
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=p;ID3DBlob*b=nullptr,*err=nullptr;auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&err);if(err)err->Release();check(hr);check(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)));b->Release();b=nullptr;err=nullptr;
  hr=CompileNativeShader(dir+L"\\native_rgb_texture.hlsl",nullptr,"main",&b,&err);if(err)err->Release();check(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={b->GetBufferPointer(),b->GetBufferSize()};hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso));b->Release();check(hr);
 }
 // Input is the network output in NON_PIXEL_SHADER_RESOURCE throughout.
 // Consumers must restore Output to NON_PIXEL_SHADER_RESOURCE after use.
 void Record(ID3D12GraphicsCommandList*c){
  if(!c||!pso)throw std::runtime_error("RGB texture unavailable");if(recorded)transition(c,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());c->Dispatch(120,68,1);
  transition(c,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);recorded=true;
 }
 ID3D12Resource*Output()const{return output;}
};
