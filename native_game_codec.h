#pragma once
#include "native_game_rgb_input.h"
#include <array>
// Validated mode1 math, fixed1080p float16 textures. Caller owns queue ordering.
// This is a resource stage, not a game callback or a history-feedback policy.
class NativeGameCodec {
 ID3D12Resource*source[3]{};ID3D12Resource*output{};
 ID3D12DescriptorHeap*heap{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT count{};bool recorded{};
 static void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("codec HRESULT="+std::to_string(unsigned(hr)));}
 static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
  if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);
 }
public:
 NativeGameCodec()=default;NativeGameCodec(const NativeGameCodec&)=delete;
 ~NativeGameCodec(){for(auto*r:source)if(r)r->Release();if(output)output->Release();if(heap)heap->Release();if(root)root->Release();if(pso)pso->Release();}
 // Encode: {linear original}. Decode: {encoded proxy, encoded neural, linear original}.
 void Create(ID3D12Device*d,const std::vector<ID3D12Resource*>&inputs,const std::wstring&dir){
  if(count||!d||(inputs.size()!=1&&inputs.size()!=3))throw std::runtime_error("codec initialization contract");
  for(size_t i=0;i<inputs.size();i++){
   auto*r=inputs[i];if(!r)throw std::runtime_error("codec null input");auto desc=r->GetDesc();
   if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("codec unverified input format/geometry");
   for(size_t j=0;j<i;j++)if(inputs[j]==r)throw std::runtime_error("codec aliased inputs");
   ID3D12Device*owner=nullptr;check(r->GetDevice(IID_PPV_ARGS(&owner)));bool same=owner==d;owner->Release();if(!same)throw std::runtime_error("codec device mismatch");
  }
  count=UINT(inputs.size());for(UINT i=0;i<count;i++){source[i]=inputs[i];source[i]->AddRef();}
  auto desc=source[0]->GetDesc();desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,count+1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
  UINT stride=d->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=desc.Format;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
  for(UINT i=0;i<count;i++){d->CreateShaderResourceView(source[i],&sv,cpu);cpu.ptr+=stride;}
  D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=desc.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(output,nullptr,&uv,cpu);
  D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,count,count==3?1u:0u,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,count}};
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,16};
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;ID3DBlob*b=nullptr,*err=nullptr;auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&err);if(err)err->Release();check(hr);check(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)));b->Release();b=nullptr;err=nullptr;
  hr=CompileNativeShader(dir+(count==3?L"\\native_codec_decode.hlsl":L"\\native_codec_encode.hlsl"),nullptr,"main",&b,&err);if(err)err->Release();check(hr);
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={b->GetBufferPointer(),b->GetBufferSize()};hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso));b->Release();check(hr);
 }
 // Caller must have completed every GPU use of this stage before rebinding.
 void RebindInputAfterCompletion(UINT index,ID3D12Resource*replacement){
  if(!pso||index>=count||!replacement)throw std::runtime_error("codec rebind contract");
  if(replacement==source[index])return;
  auto desc=replacement->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("codec rebind geometry/format");
  if(replacement==output)throw std::runtime_error("codec rebind output alias");
  for(UINT i=0;i<count;i++)if(i!=index&&source[i]==replacement)throw std::runtime_error("codec rebind input alias");
  ID3D12Device*d=nullptr,*owner=nullptr;check(heap->GetDevice(IID_PPV_ARGS(&d)));
  auto hr=replacement->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr)){d->Release();check(hr);}
  bool same=owner==d;owner->Release();if(!same){d->Release();throw std::runtime_error("codec rebind device mismatch");}
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=desc.Format;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
  auto cpu=heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(index)*d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  replacement->AddRef();d->CreateShaderResourceView(replacement,&sv,cpu);d->Release();source[index]->Release();source[index]=replacement;
 }
 void Record(ID3D12GraphicsCommandList*c,const std::vector<D3D12_RESOURCE_STATES>&before,float paper_white=1.f){
  if(!c||!pso||before.size()!=count||(paper_white!=1.f&&paper_white!=.5f&&paper_white!=2.f))throw std::runtime_error("codec unverified record contract");
  if(recorded)transition(c,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  for(UINT i=0;i<count;i++)transition(c,source[i],before[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  uint32_t words[16]={1920,1080,1920,1080,0,0,1920,1080,0,0x3f800000,0x3f800000,1};std::memcpy(words+8,&paper_white,4);
  c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,16,words,0);c->Dispatch(120,68,1);
  transition(c,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  for(UINT i=0;i<count;i++)transition(c,source[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,before[i]);recorded=true;
 }
 ID3D12Resource*Output()const{return output;}
};
