#pragma once
#include "native_rgb_reflect.h"

// One stable texture binding per instance. Caller must fence all users before
// destroying this object or modifying the texture. Does not submit game lists.
class NativeGameRgbInput {
 ID3D12Resource *source{},*tiles{},*color{};
 ID3D12DescriptorHeap*heap{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 bool recorded{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("game RGB HRESULT="+std::to_string(unsigned(h)));}
 static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
  if(a==b)return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&t);
 }
public:
 NativeGameRgbInput()=default;NativeGameRgbInput(const NativeGameRgbInput&)=delete;
 ~NativeGameRgbInput(){for(auto*r:{source,tiles,color})if(r)r->Release();if(heap)heap->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*texture,const std::wstring&dir){
  if(source||!d||!texture)throw std::runtime_error("game RGB initialization");
  auto desc=texture->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("game RGB texture geometry");
  // Initially accept only explicit float formats; other game formats need proof.
  if(desc.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)throw std::runtime_error("unverified game RGB format");
  ID3D12Device*owner=nullptr;ck(texture->GetDevice(IID_PPV_ARGS(&owner)));bool same=owner==d;owner->Release();if(!same)throw std::runtime_error("game RGB device mismatch");
  source=texture;source->AddRef();
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC bd{};
  bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=1920ull*1152*16;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  for(auto**r:{&tiles,&color})ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(r)));
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ck(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=desc.Format;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;d->CreateShaderResourceView(source,&sv,heap->GetCPUDescriptorHandleForHeapStart());
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER p[3]{};
  p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
  for(UINT i=1;i<3;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[i].Descriptor.ShaderRegister=i-1;}
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=3;rd.pParameters=p;ID3DBlob*blob=nullptr,*error=nullptr;
  auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);if(error)error->Release();ck(hr);ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();blob=nullptr;error=nullptr;
  hr=CompileNativeShader(dir+L"\\native_game_rgb_input.hlsl",nullptr,"main",&blob,&error);if(error)error->Release();ck(hr);
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 // before is supplied by the owner; the source returns to exactly that state.
 void Record(ID3D12GraphicsCommandList*c,D3D12_RESOURCE_STATES before){
  if(!pso||!c)throw std::runtime_error("game RGB not initialized");
  if(recorded)for(auto*r:{tiles,color})transition(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  transition(c,source,before,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);c->SetPipelineState(pso);
  c->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
  c->SetComputeRootUnorderedAccessView(1,tiles->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,color->GetGPUVirtualAddress());c->Dispatch(240,144,1);
  for(auto*r:{tiles,color})transition(c,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  transition(c,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,before);recorded=true;
 }
 ID3D12Resource*Tiles()const{return tiles;}
 ID3D12Resource*PostBase()const{return color;}
};
