#pragma once
#include <vector>
#include "native_pso.h"
#include "native_lab_paths.h"
#include "native_pinned_resource.h"
#include "native_device_identity.h"
#include "native_rgb_reflect.h"
#include "native_network_geometry.h"

// One stable texture binding per instance. Caller must fence all users before
// destroying this object or modifying the texture. Does not submit game lists.
class NativeGameRgbInput {
 ID3D12Resource *source{},*tiles{},*color{};
 ID3D12DescriptorHeap*heap{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 ID3D12Resource*external{};bool no_tiles{};
 bool recorded{};NativeNetworkGeometry geometry=NativeNetworkGeometry::FromHeight(1080);
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("game RGB HRESULT="+std::to_string(unsigned(h)));}
 static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
  if(a==b)return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&t);
 }
 bool replayable_recording{};
public:
 void EnableReplayableRecording(){if(source)throw std::runtime_error("replayable recording must precede Create");replayable_recording=true;}

 NativeGameRgbInput()=default;NativeGameRgbInput(const NativeGameRgbInput&)=delete;
 ~NativeGameRgbInput(){for(auto*r:{tiles,color})NativeUntrackResource(r);for(auto*r:{source,tiles,color,external})if(r)r->Release();if(heap)heap->Release();if(root)root->Release();if(pso)pso->Release();}
 /* tiles=false (HIP backend): the tile-ordered copy is never read, so the shader skips it and the buffer is not allocated. */
 void Create(ID3D12Device*d,ID3D12Resource*texture,const std::wstring&dir,bool tiles_needed=true,NativeShaderCompiler*compiler=nullptr){
  if(source||!d||!texture)throw std::runtime_error("game RGB initialization");
  geometry=NativeCurrentNetworkGeometry();auto desc=texture->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=geometry.valid_width||desc.Height!=geometry.valid_height||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("game RGB texture geometry");
  // Initially accept only explicit float formats; other game formats need proof.
  if(NativeViewFormat(desc.Format)!=DXGI_FORMAT_R32G32B32A32_FLOAT&&!NativeIsGameColor(desc.Format))throw std::runtime_error("unverified game RGB format");
  ID3D12Device*owner=nullptr;ck(texture->GetDevice(IID_PPV_ARGS(&owner)));bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("game RGB device mismatch");
  source=texture;source->AddRef();
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC bd{};
  bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=UINT64(geometry.processing_width)*geometry.processing_height*16;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  no_tiles=!tiles_needed;
  for(auto**r:{&tiles,&color})if(!(no_tiles&&r==&tiles))ck(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&bd,replayable_recording?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(r)));
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ck(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=NativeViewFormat(desc.Format);sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;d->CreateShaderResourceView(source,&sv,heap->GetCPUDescriptorHandleForHeapStart());
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER p[3]{};
  p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
  for(UINT i=1;i<3;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[i].Descriptor.ShaderRegister=i-1;}
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=3;rd.pParameters=p;ID3DBlob*blob=nullptr,*error=nullptr;
  auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);if(error)error->Release();ck(hr);ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();blob=nullptr;error=nullptr;
  const D3D_SHADER_MACRO nt[]={{"NATIVE_RGB_NO_TILES","1"},{nullptr,nullptr}};const std::string ph=std::to_string(geometry.processing_height);const D3D_SHADER_MACRO fr[]={{"NATIVE_RGB_PROCESSING_HEIGHT",ph.c_str()},{no_tiles?"NATIVE_RGB_NO_TILES":nullptr,"1"},{nullptr,nullptr}}; /* free geometry: padded height from the host */
  hr=CompileNativeShader(dir+L"\\native_game_rgb_input.hlsl",geometry.Free()?fr:no_tiles?nt:nullptr,"main",&blob,&error,compiler);if(error)error->Release();ck(hr);
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 // before is supplied by the owner; the source returns to exactly that state.
 void Record(ID3D12GraphicsCommandList*c,D3D12_RESOURCE_STATES before){
  if(!pso||!c)throw std::runtime_error("game RGB not initialized");
  /* external (direct input): the shared network input rests in COMMON between frames (HIP reads it after the bridge fence) */
  ID3D12Resource*out=external?external:color;const D3D12_RESOURCE_STATES rest=external?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  if(external)transition(c,out,rest,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(replayable_recording||recorded)for(auto*r:{tiles,color})if(r&&!(external&&r==color))transition(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  transition(c,source,before,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);c->SetPipelineState(pso);
  c->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
  c->SetComputeRootUnorderedAccessView(1,(tiles?tiles:out)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,out->GetGPUVirtualAddress());c->Dispatch(geometry.processing_width/8,geometry.processing_height/8,1);
  for(auto*r:{tiles,color})if(r&&!(external&&r==color))transition(c,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if(external)transition(c,out,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,rest);
  transition(c,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,before);recorded=true;
 }
 /* Direct input: write the network input into the bridge's shared buffer (created with UAV, resting in COMMON) instead of our own
    buffer, so the bridge needs no copy. Before the first Record only; the private buffer is kept unused (callers may hold it). */
 void RedirectOutput(ID3D12Resource*shared){
  if(recorded||external||!shared)throw std::runtime_error("game RGB redirect order");
  if(shared->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||shared->GetDesc().Width<UINT64(geometry.processing_width)*geometry.processing_height*16||!(shared->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))throw std::runtime_error("game RGB redirect target");
  external=shared;external->AddRef();
 }
 ID3D12Resource*Tiles()const{return tiles;}
 void PinRecording(std::vector<IUnknown*>&pins)const{
  IUnknown*objects[]={heap,root,pso,source,tiles,color,external};
  for(auto*p:objects)if(p){pins.push_back(p);p->AddRef();}
 }
 ID3D12Resource*PostBase()const{return external?external:color;}
};
