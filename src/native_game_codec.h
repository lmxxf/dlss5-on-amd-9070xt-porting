#pragma once
#include "native_pso.h"
#include "native_lab_paths.h"
#include "native_pinned_resource.h"
#include "native_device_identity.h"
#include "native_game_rgb_input.h"
#include <array>
// Validated mode1 math, fixed1080p float16 textures. Caller owns queue ordering.
// This is a resource stage, not a game callback or a history-feedback policy.
class NativeGameCodec {
 ID3D12Resource*source[3]{};ID3D12Resource*output{};
 ID3D12DescriptorHeap*heap{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT count{};bool recorded{};
 bool unorm_out{},unorm8_out{};DXGI_FORMAT out_format{};
 static void step(ID3D12Device*d,const char*what){if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=codec_step detail=%s removed=%08x\n",GetCurrentProcessId(),GetTickCount64(),what,unsigned(d->GetDeviceRemovedReason()));fclose(f);}}
 static void check(HRESULT hr,const char*what="?"){if(FAILED(hr))throw std::runtime_error(std::string("codec ")+what+" HRESULT="+std::to_string(unsigned(hr)));}
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
   if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||!NativeIsGameColor(desc.Format)||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("codec unverified input format/geometry");
   for(size_t j=0;j<i;j++)if(inputs[j]==r)throw std::runtime_error("codec aliased inputs");
   ID3D12Device*owner=nullptr;check(r->GetDevice(IID_PPV_ARGS(&owner)),"input-getdevice");bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("codec device mismatch");
  }
  step(d,"inputs-checked");
  count=UINT(inputs.size());for(UINT i=0;i<count;i++){source[i]=inputs[i];source[i]->AddRef();}
  auto desc=source[0]->GetDesc();desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  /* Typeless game textures: the encoder's output (our intermediate) is FP16; the decoder's output is copied raw into the game texture, so it takes the game's interpretation (UNORM for Ronin). */
  unorm_out=count==3&&NativeViewFormat(source[2]->GetDesc().Format)==DXGI_FORMAT_R16G16B16A16_UNORM;
  /* 8-bit UNORM game texture (Magpie): UNORM8 bits in a raw buffer (row pitch 1920*4), copied into the texture by the frame; BGRA order for B8G8R8A8. */
  unorm8_out=count==3&&NativeIsRgba8Unorm(source[2]->GetDesc().Format);out_format=count==3?NativeViewFormat(source[2]->GetDesc().Format):DXGI_FORMAT_UNKNOWN;
  desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  if(unorm8_out){desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=1920ull*1080*4;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;}
  else if(unorm_out){desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=1920ull*1080*8;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;} /* UNORM bits in a raw buffer (this driver device-removes on non-float RGBA16 typed UAVs); the frame copies it into the game texture */ /* UNORM bits are written through a UINT UAV (the driver device-removes on a UNORM typed UAV) */
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;check(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)),"output");step(d,"output-created");
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,count+1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"heap");
  UINT stride=d->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=NativeViewFormat(desc.Format);sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
  for(UINT i=0;i<count;i++){sv.Format=NativeViewFormat(source[i]->GetDesc().Format);/* per input: the game texture may be typeless, the intermediates are FP16 */d->CreateShaderResourceView(source[i],&sv,cpu);cpu.ptr+=stride;}step(d,"srvs");
  D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};if(unorm_out||unorm8_out){uv.Format=DXGI_FORMAT_R32_TYPELESS;uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=1920u*1080*(unorm8_out?1:2);uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;}else{uv.Format=NativeViewFormat(desc.Format);uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;}d->CreateUnorderedAccessView(output,nullptr,&uv,cpu);step(d,"uav");
  D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,count,count==3?1u:0u,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,count}};
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,16};
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;ID3DBlob*b=nullptr,*err=nullptr;auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&err);if(err)err->Release();check(hr,"rootsig-serialize");step(d,"rootsig-serialized");check(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)),"rootsig");step(d,"rootsig");b->Release();b=nullptr;err=nullptr;
  /* DLSS5_CODEC_SRGB=1 (Magpie): the host texture is a display-referred sRGB picture: the encoder passes it through, the decoder linearizes and re-encodes. */
  const char*srgb_io=(_wgetenv(L"DLSS5_CODEC_SRGB")&&!wcscmp(_wgetenv(L"DLSS5_CODEC_SRGB"),L"1"))?"1":"0";
  const D3D_SHADER_MACRO uint_out[]={{"NATIVE_CODEC_UINT_OUT","1"},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}},unorm8[]={{"NATIVE_CODEC_UNORM8_OUT","1"},{"NATIVE_CODEC_BGRA",(out_format==DXGI_FORMAT_B8G8R8A8_UNORM||out_format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)?"1":"0"},{"NATIVE_CODEC_DEBUG_TINT",(_wgetenv(L"DLSS5_DEBUG_TINT")&&!wcscmp(_wgetenv(L"DLSS5_DEBUG_TINT"),L"1"))?"1":"0"},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}},plain[]={{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}}; /* DLSS5_DEBUG_TINT=1 (diagnostic): the UNORM8 path writes a magenta-tinted picture so the write-back is visible */hr=CompileNativeShader(dir+(count==3?L"\\native_codec_decode.hlsl":L"\\native_codec_encode.hlsl"),unorm8_out?unorm8:unorm_out?uint_out:plain,"main",&b,&err);if(err)err->Release();check(hr,"compile");step(d,"compiled");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={b->GetBufferPointer(),b->GetBufferSize()};hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso));b->Release();check(hr,"pso");
 }
 // Caller must have completed every GPU use of this stage before rebinding.
 void RebindInputAfterCompletion(UINT index,ID3D12Resource*replacement){
  if(!pso||index>=count||!replacement)throw std::runtime_error("codec rebind contract");
  if(replacement==source[index])return;
  auto desc=replacement->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||!NativeIsGameColor(desc.Format)||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("codec rebind geometry/format");
  if(replacement==output)throw std::runtime_error("codec rebind output alias");
  for(UINT i=0;i<count;i++)if(i!=index&&source[i]==replacement)throw std::runtime_error("codec rebind input alias");
  ID3D12Device*d=nullptr,*owner=nullptr;check(heap->GetDevice(IID_PPV_ARGS(&d)));
  auto hr=replacement->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr)){d->Release();check(hr);}
  bool same=NativeSameDevice(owner,d);owner->Release();if(!same){d->Release();throw std::runtime_error("codec rebind device mismatch");}
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=NativeViewFormat(desc.Format);sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
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
 ID3D12Resource*Output()const{return output;}bool BufferOutput()const{return unorm_out||unorm8_out;}
 /* footprint of the raw output buffer for CopyTextureRegion into the game texture */
 D3D12_SUBRESOURCE_FOOTPRINT BufferFootprint()const{return unorm8_out?D3D12_SUBRESOURCE_FOOTPRINT{out_format,1920,1080,1,1920*4}:D3D12_SUBRESOURCE_FOOTPRINT{DXGI_FORMAT_R16G16B16A16_UNORM,1920,1080,1,1920*8};}
};
