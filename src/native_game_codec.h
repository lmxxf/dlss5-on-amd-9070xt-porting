#pragma once
#include "native_pso.h"
#include "native_lab_paths.h"
#include "native_pinned_resource.h"
#include "native_device_identity.h"
#include "native_shader_cache.h"
#include <cmath>
#include <cstdint>
#include <array>
#include "native_input_geometry.h"
#include "native_network_geometry.h"
enum class NativeCodecDebugView : uint32_t { Final=0, Proxy=1, Neural=2, Difference=3, Tint=4 };
struct NativeCodecParameters {
 float transfer_strength=1.f,color_strength=1.f;
 NativeCodecDebugView debug_view=NativeCodecDebugView::Final;
 float pre_exposure=1.f,exposure_scale=1.f;
 // No usable game exposure texture: estimate white point from image mean.
 // Packed into the debug_view cbuffer word high bit (0x10000).
 bool auto_white=false;
 // Opt-in integration policies; defaults preserve the legacy codec output.
 bool use_pre_exposure=false; // no exposure texture: use the host pre-exposure
 bool hue_safe=false; // CS 0..1 preserves game chroma; CS 2 reaches legacy CS 1
 bool Valid()const{return std::isfinite(pre_exposure)&&pre_exposure>0&&std::isfinite(exposure_scale)&&exposure_scale>0&&ValidStrength();}
 bool ValidStrength()const{return std::isfinite(transfer_strength)&&std::isfinite(color_strength)&&transfer_strength>=0.f&&transfer_strength<=3.f&&color_strength>=0.f&&color_strength<=3.f&&uint32_t(debug_view)<=4;}
};
// Validated mode1 math, fixed1080p float16 textures. Caller owns queue ordering.
// This is a resource stage, not a game callback or a history-feedback policy.
class NativeGameCodec {
 ID3D12Resource*source[3]{};ID3D12Resource*output{};ID3D12Resource*exposure_texture{};
 ID3D12DescriptorHeap*heap{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 struct Binding {ID3D12DescriptorHeap*heap;std::array<ID3D12Resource*,3> sources;};
 std::vector<Binding> bindings;
 static constexpr size_t binding_limit=8;
 void ClearBindings(){for(auto&b:bindings){b.heap->Release();for(auto*r:b.sources)if(r)r->Release();}bindings.clear();}
 UINT count{};bool recorded{};NativeInputGeometry geometry{};UINT out_width{},out_height{},row_pitch{};
 bool private_float_output{};
 bool unorm_out{},unorm8_out{},r11_out{},r10_out{};DXGI_FORMAT out_format{};
 static void step(ID3D12Device*d,const char*what){if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=codec_step detail=%s removed=%08x\n",GetCurrentProcessId(),GetTickCount64(),what,unsigned(d->GetDeviceRemovedReason()));fclose(f);}}
 static void check(HRESULT hr,const char*what="?"){if(FAILED(hr))throw std::runtime_error(std::string("codec ")+what+" HRESULT="+std::to_string(unsigned(hr)));}
 static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
  if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);
 }
 void WriteExposureView(ID3D12Device*d,ID3D12DescriptorHeap*h){
  if(!exposure_texture)return;
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=exposure_texture->GetDesc().Format;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
  auto cpu=h->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(count+1)*d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);d->CreateShaderResourceView(exposure_texture,&sv,cpu);
 }
public:
 NativeGameCodec()=default;NativeGameCodec(const NativeGameCodec&)=delete;
 ~NativeGameCodec(){NativeUntrackResource(output);if(exposure_texture)exposure_texture->Release();ClearBindings();for(auto*r:source)if(r)r->Release();if(output)output->Release();if(heap)heap->Release();if(root)root->Release();if(pso)pso->Release();}
 // Encode: {linear original}. Decode: {encoded proxy, encoded neural, linear original}.
 void Create(ID3D12Device*d,const std::vector<ID3D12Resource*>&inputs,const std::wstring&dir,bool privateFloatOutput=false,ID3D12Resource*exposure=nullptr){
  private_float_output=privateFloatOutput;
  if(count||!d||(inputs.size()!=1&&inputs.size()!=3))throw std::runtime_error("codec initialization contract");
  if(exposure){
   const auto ed=exposure->GetDesc();
   if(ed.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||ed.Width!=1||ed.Height!=1||ed.MipLevels!=1||ed.DepthOrArraySize!=1||ed.SampleDesc.Count!=1||(ed.Format!=DXGI_FORMAT_R16_FLOAT&&ed.Format!=DXGI_FORMAT_R32_FLOAT)||ed.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)throw std::runtime_error("codec exposure format");
   ID3D12Device*owner=nullptr;check(exposure->GetDevice(IID_PPV_ARGS(&owner)));const bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("codec exposure device");
   exposure_texture=exposure;exposure_texture->AddRef();
  }
  const auto network=NativeCurrentNetworkGeometry();
  for(size_t i=0;i<inputs.size();i++){
   auto*r=inputs[i];if(!r)throw std::runtime_error("codec null input");auto desc=r->GetDesc();
   if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||!NativeInputGeometry::Supported(desc.Width,desc.Height,NativeFitLargeInput())||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||!(NativeIsGameColor(desc.Format)||(private_float_output&&desc.Format==DXGI_FORMAT_R9G9B9E5_SHAREDEXP))||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("codec unverified input format/geometry");
   if(inputs.size()==3&&i<2&&(desc.Width!=network.valid_width||desc.Height!=network.valid_height))throw std::runtime_error("codec network surface geometry");
   for(size_t j=0;j<i;j++)if(inputs[j]==r)throw std::runtime_error("codec aliased inputs");
   ID3D12Device*owner=nullptr;check(r->GetDevice(IID_PPV_ARGS(&owner)),"input-getdevice");bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("codec device mismatch");
  }
  step(d,"inputs-checked");
  count=UINT(inputs.size());for(UINT i=0;i<count;i++){source[i]=inputs[i];source[i]->AddRef();}
  auto external=source[count==3?2:0]->GetDesc();geometry=NativeInputGeometry::Make(unsigned(external.Width),external.Height,network.valid_width,network.valid_height,NativeFitLargeInput());
  out_width=count==3?geometry.width:network.valid_width;out_height=count==3?geometry.height:network.valid_height;
  auto desc=source[0]->GetDesc();desc.Width=out_width;desc.Height=out_height;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  /* Typeless game textures: the encoder's output (our intermediate) is FP16; the decoder's output is copied raw into the game texture, so it takes the game's interpretation (UNORM for Ronin). */
  unorm_out=!private_float_output&&count==3&&NativeViewFormat(source[2]->GetDesc().Format)==DXGI_FORMAT_R16G16B16A16_UNORM;
  /* 8-bit UNORM game texture (Magpie): UNORM8 bits in a raw buffer (row pitch 1920*4), copied into the texture by the frame; BGRA order for B8G8R8A8. */
  unorm8_out=!private_float_output&&count==3&&NativeIsRgba8Unorm(source[2]->GetDesc().Format);r11_out=!private_float_output&&count==3&&NativeIsR11G11B10(source[2]->GetDesc().Format);r10_out=!private_float_output&&count==3&&NativeIsR10G10B10A2(source[2]->GetDesc().Format);out_format=private_float_output?DXGI_FORMAT_R16G16B16A16_FLOAT:count==3?NativeViewFormat(source[2]->GetDesc().Format):DXGI_FORMAT_UNKNOWN;
  row_pitch=geometry.RowPitch((unorm8_out||r11_out||r10_out)?4:8);
  desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  if(unorm8_out||r11_out||r10_out){desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=UINT64(row_pitch)*out_height;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;}
  else if(unorm_out){desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=UINT64(row_pitch)*out_height;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;} /* UNORM bits in a raw buffer (this driver device-removes on non-float RGBA16 typed UAVs); the frame copies it into the game texture */ /* UNORM bits are written through a UINT UAV (the driver device-removes on a UNORM typed UAV) */
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;check(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)),"output");step(d,"output-created");
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,count+1+(exposure_texture?1u:0u),D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"heap");
  UINT stride=d->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=NativeViewFormat(desc.Format);sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
  for(UINT i=0;i<count;i++){sv.Format=NativeViewFormat(source[i]->GetDesc().Format);/* per input: the game texture may be typeless, the intermediates are FP16 */d->CreateShaderResourceView(source[i],&sv,cpu);cpu.ptr+=stride;}step(d,"srvs");
  D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};if(unorm_out||unorm8_out||r11_out||r10_out){uv.Format=DXGI_FORMAT_R32_TYPELESS;uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=row_pitch*out_height/4;uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;}else{uv.Format=NativeViewFormat(desc.Format);uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;}d->CreateUnorderedAccessView(output,nullptr,&uv,cpu);step(d,"uav");
  WriteExposureView(d,heap);
  D3D12_DESCRIPTOR_RANGE ranges[3]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,count,count==3?1u:0u,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,count},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,4,0,count+1}};
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={exposure_texture?3u:2u,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,20};
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;ID3DBlob*b=nullptr,*err=nullptr;auto hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&err);if(err)err->Release();check(hr,"rootsig-serialize");step(d,"rootsig-serialized");check(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)),"rootsig");step(d,"rootsig");b->Release();b=nullptr;err=nullptr;
  /* DLSS5_CODEC_SRGB=1 (Magpie): the host texture is a display-referred sRGB picture: the encoder passes it through, the decoder linearizes and re-encodes. */
  const char*srgb_io=(_wgetenv(L"DLSS5_CODEC_SRGB")&&!wcscmp(_wgetenv(L"DLSS5_CODEC_SRGB"),L"1"))?"1":"0";
  const char*fit=geometry.Adapted()?"1":"0";
  const D3D_SHADER_MACRO uint_out[]={{"NATIVE_CODEC_EXPOSURE",exposure_texture?"1":"0"},{"NATIVE_CODEC_FIT",fit},{"NATIVE_CODEC_UINT_OUT","1"},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}},unorm8[]={{"NATIVE_CODEC_EXPOSURE",exposure_texture?"1":"0"},{"NATIVE_CODEC_FIT",fit},{"NATIVE_CODEC_UNORM8_OUT","1"},{"NATIVE_CODEC_BGRA",(out_format==DXGI_FORMAT_B8G8R8A8_UNORM||out_format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)?"1":"0"},{"NATIVE_CODEC_DEBUG_TINT",(_wgetenv(L"DLSS5_DEBUG_TINT")&&!wcscmp(_wgetenv(L"DLSS5_DEBUG_TINT"),L"1"))?"1":"0"},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}},plain[]={{"NATIVE_CODEC_EXPOSURE",exposure_texture?"1":"0"},{"NATIVE_CODEC_FIT",fit},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}},r11[]={{"NATIVE_CODEC_EXPOSURE",exposure_texture?"1":"0"},{"NATIVE_CODEC_FIT",fit},{"NATIVE_CODEC_R11_OUT","1"},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}},r10[]={{"NATIVE_CODEC_EXPOSURE",exposure_texture?"1":"0"},{"NATIVE_CODEC_FIT",fit},{"NATIVE_CODEC_R10_OUT","1"},{"NATIVE_CODEC_SRGB_IO",srgb_io},{nullptr,nullptr}}; /* DLSS5_DEBUG_TINT=1 (diagnostic): the UNORM8 path writes a magenta-tinted picture so the write-back is visible */hr=CompileNativeShader(dir+(count==3?L"\\native_codec_decode.hlsl":L"\\native_codec_encode.hlsl"),unorm8_out?unorm8:r11_out?r11:r10_out?r10:unorm_out?uint_out:plain,"main",&b,&err);if(FAILED(hr)){std::string message=count==3?"codec decode compile failed: ":"codec encode compile failed: ";if(err)message.append(static_cast<const char*>(err->GetBufferPointer()),err->GetBufferSize());if(err)err->Release();if(b)b->Release();throw std::runtime_error(message+" HRESULT="+std::to_string(unsigned(hr)));}if(err)err->Release();step(d,"compiled");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={b->GetBufferPointer(),b->GetBufferSize()};hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso));b->Release();check(hr,"pso");
 }
 // Cached heaps are immutable and retain their resources. A full cache requires
 // completion before eviction; the current heap remains owned separately.
 bool RebindNeedsCompletion(UINT index,ID3D12Resource*replacement)const{
  if(index>=count)return true;
  for(const auto&b:bindings){bool match=true;for(UINT i=0;i<count;i++)if(b.sources[i]!=(i==index?replacement:source[i]))match=false;if(match)return false;}
  return bindings.size()>=binding_limit;
 }
 // Caller completes GPU uses only when RebindNeedsCompletion reports eviction.
 void RebindInputAfterCompletion(UINT index,ID3D12Resource*replacement){
  if(!pso||index>=count||!replacement)throw std::runtime_error("codec rebind contract");
  if(replacement==source[index])return;
  auto desc=replacement->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=source[index]->GetDesc().Width||desc.Height!=source[index]->GetDesc().Height||desc.Format!=source[index]->GetDesc().Format||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||!(NativeIsGameColor(desc.Format)||(private_float_output&&desc.Format==DXGI_FORMAT_R9G9B9E5_SHAREDEXP))||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::runtime_error("codec rebind geometry/format");
  if(replacement==output)throw std::runtime_error("codec rebind output alias");
  for(UINT i=0;i<count;i++)if(i!=index&&source[i]==replacement)throw std::runtime_error("codec rebind input alias");
  ID3D12Device*d=nullptr,*owner=nullptr;check(heap->GetDevice(IID_PPV_ARGS(&d)));
  auto hr=replacement->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr)){d->Release();check(hr);}
  bool same=NativeSameDevice(owner,d);owner->Release();if(!same){d->Release();throw std::runtime_error("codec rebind device mismatch");}
  for(const auto&b:bindings){bool match=true;for(UINT i=0;i<count;i++)if(b.sources[i]!=(i==index?replacement:source[i]))match=false;
   if(match){b.heap->AddRef();heap->Release();heap=b.heap;replacement->AddRef();source[index]->Release();source[index]=replacement;d->Release();return;}}
  if(bindings.size()>=binding_limit)ClearBindings(); // caller has waited
  bool retained=false;for(const auto&b:bindings)if(b.heap==heap)retained=true;
  if(!retained){Binding saved{heap,{source[0],source[1],source[2]}};saved.heap->AddRef();for(auto*r:saved.sources)if(r)r->AddRef();bindings.push_back(saved);}
  ID3D12DescriptorHeap*next=nullptr;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,count+1+(exposure_texture?1u:0u),D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
  auto create_hr=d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&next));if(FAILED(create_hr)){d->Release();check(create_hr,"rebind heap");}
  heap->Release();heap=next;
  D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
  auto cursor=heap->GetCPUDescriptorHandleForHeapStart();const UINT stride=d->GetDescriptorHandleIncrementSize(hd.Type);
  for(UINT i=0;i<count;i++){auto*r=i==index?replacement:source[i];sv.Format=NativeViewFormat(r->GetDesc().Format);d->CreateShaderResourceView(r,&sv,cursor);cursor.ptr+=stride;}
  D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};if(unorm_out||unorm8_out||r11_out||r10_out){uv.Format=DXGI_FORMAT_R32_TYPELESS;uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=row_pitch*out_height/4;uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;}else{uv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;}d->CreateUnorderedAccessView(output,nullptr,&uv,cursor);WriteExposureView(d,heap);
  sv.Format=NativeViewFormat(desc.Format);
  auto cpu=heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(index)*d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  replacement->AddRef();d->CreateShaderResourceView(replacement,&sv,cpu);d->Release();source[index]->Release();source[index]=replacement;
  Binding fresh{heap,{source[0],source[1],source[2]}};fresh.heap->AddRef();for(auto*r:fresh.sources)if(r)r->AddRef();bindings.push_back(fresh);
 }
 // Preserve legacy DLSS5_STRENGTH semantics for existing callers. Explicit parameters
 // override them for this dispatch only, allowing a host UI to update every frame.
 static NativeCodecParameters LegacyParameters(){
  static const std::array<float,2>strength=[]{std::array<float,2>v{1.f,1.f};const wchar_t*e=_wgetenv(L"DLSS5_STRENGTH");float a=1.f,b=1.f;
   if(e&&swscanf(e,L"%f,%f",&a,&b)==2&&a>=0.f&&a<=3.f&&b>=0.f&&b<=3.f){v[0]=a;v[1]=b;} /* 2026-09-24: >1 extrapolates (lerp past the network result) -- diagnostic only, makes the network's contribution visible */
   else{ /* unset or "auto" (the shipped template since 0.30): per-title quirk table. The pre-upscale route hands the network the
            linear colour buffer *before* the game's tone mapper and colour grading; taking the network's hue there and then
            running it through the game's LUT turns the hue (Cyberpunk 2077 2.31: green neon ambient became brown, R +14% G -16%).
            Luminance transfer alone keeps the detail gain (+19% vs +22% high-pass) with the game's own hue. Measured 2026-09-24. */
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);const wchar_t*base=wcsrchr(exe,L'\\');base=base?base+1:exe;
    struct{const wchar_t*exe;float transfer,color;}static const table[]={{L"Cyberpunk2077.exe",1.f,0.f}};const wchar_t*hit=nullptr;
    for(const auto&t:table)if(!_wcsicmp(base,t.exe)){v[0]=t.transfer;v[1]=t.color;hit=t.exe;}
    if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=strength detail=auto exe=%ls -> %g,%g%s\n",GetCurrentProcessId(),GetTickCount64(),base,v[0],v[1],hit?" (quirk)":"");fclose(f);}}
   return v;}();
  return {strength[0],strength[1],NativeCodecDebugView::Final};
 }
 void Record(ID3D12GraphicsCommandList*c,const std::vector<D3D12_RESOURCE_STATES>&before,float paper_white=1.f){Record(c,before,paper_white,LegacyParameters());}
 void Record(ID3D12GraphicsCommandList*c,const std::vector<D3D12_RESOURCE_STATES>&before,float paper_white,const NativeCodecParameters&parameters){
  if(!c||!pso||before.size()!=count+(exposure_texture?1u:0u)||!(paper_white>0.f&&paper_white<=64.f&&paper_white==paper_white) /* 2026-09-24: any finite positive scale (DLSS5_PAPER_WHITE); was {0.5,1,2} */||!parameters.Valid())throw std::runtime_error("codec unverified record contract");
  if(recorded)transition(c,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  for(UINT i=0;i<count;i++)transition(c,source[i],before[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if(exposure_texture)transition(c,exposure_texture,before.back(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  uint32_t words[20]={out_width,out_height,geometry.width,geometry.height,0,0,geometry.network_width,geometry.network_height,0,0x3f800000,0x3f800000,1};
  const float viewport[]={float(geometry.x),float(geometry.y),float(geometry.fit_width),float(geometry.fit_height)};std::memcpy(words+12,viewport,sizeof viewport);words[16]=row_pitch;std::memcpy(words+8,&paper_white,4);std::memcpy(words+9,&parameters.transfer_strength,4);std::memcpy(words+10,&parameters.color_strength,4);words[17]=uint32_t(parameters.debug_view)|(parameters.auto_white?0x10000u:0u)|(parameters.use_pre_exposure?0x20000u:0u)|(parameters.hue_safe?0x40000u:0u);std::memcpy(words+18,&parameters.pre_exposure,4);std::memcpy(words+19,&parameters.exposure_scale,4);
  c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,20,words,0);c->Dispatch((out_width+15)/16,(out_height+15)/16,1);
  transition(c,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  for(UINT i=0;i<count;i++)transition(c,source[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,before[i]);if(exposure_texture)transition(c,exposure_texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,before.back());recorded=true;
 }
 const NativeInputGeometry&Geometry()const{return geometry;}
 ID3D12Resource*Output()const{return output;}bool BufferOutput()const{return unorm_out||unorm8_out||r11_out||r10_out;}
 /* footprint of the raw output buffer for CopyTextureRegion into the game texture */
 D3D12_SUBRESOURCE_FOOTPRINT BufferFootprint()const{return (unorm8_out||r11_out||r10_out)?D3D12_SUBRESOURCE_FOOTPRINT{out_format,out_width,out_height,1,row_pitch}:D3D12_SUBRESOURCE_FOOTPRINT{DXGI_FORMAT_R16G16B16A16_UNORM,out_width,out_height,1,row_pitch};}
};
