#pragma once
#include "native_pso.h"
#include "native_lab_paths.h"
#include "native_shader_cache.h"
#include "native_device_identity.h"
#include "native_pinned_resource.h"
#include <d3d12.h>
#include <string>
#include <cstring>
#include <cctype>
#include <mutex>
/* Draw into a private strip, then copy to the host texture without creating a host UAV.
   Callers serialize recording and submit on one queue; unchanged text reuses the GPU strip. */
class NativeTextOverlay {
 std::mutex create_mutex;ID3D12Device*device{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};ID3D12Resource*strip{};bool failed{};
 UINT cached_words[24]{};bool cached{};
 static constexpr UINT strip_w=1280,strip_h=32,strip_pitch=1280*8; /* widest box: 64 chars x 6 px x scale 3 */
 /* host format -> (copy footprint format, shader pixel mode, bytes per pixel); DXGI_FORMAT_UNKNOWN = unsupported */
 static DXGI_FORMAT CopyFormat(DXGI_FORMAT f,UINT&mode,UINT&bpp){
  switch(f){
   case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_R8G8B8A8_TYPELESS:mode=0;bpp=4;return DXGI_FORMAT_R8G8B8A8_UNORM;
   case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_TYPELESS:mode=1;bpp=4;return DXGI_FORMAT_B8G8R8A8_UNORM;
   case DXGI_FORMAT_R16G16B16A16_FLOAT:mode=2;bpp=8;return DXGI_FORMAT_R16G16B16A16_FLOAT;
   case DXGI_FORMAT_R16G16B16A16_UNORM:case DXGI_FORMAT_R16G16B16A16_TYPELESS:mode=3;bpp=8;return DXGI_FORMAT_R16G16B16A16_UNORM;
   default:mode=0;bpp=0;return DXGI_FORMAT_UNKNOWN;
  }
 }
 bool Create(ID3D12Device*d){
  if(failed)return false;if(device)return NativeSameDevice(device,d);
  std::lock_guard<std::mutex>guard(create_mutex);if(device)return NativeSameDevice(device,d);
  try{
   device=d;device->AddRef();
   D3D12_ROOT_PARAMETER p[2]{};
   p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
   p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,24};
   D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=p;ID3DBlob*b=nullptr,*e=nullptr;
   if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&e)))throw 1;if(e)e->Release();
   if(FAILED(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root))))throw 2;b->Release();b=nullptr;e=nullptr;
   if(FAILED(CompileNativeShader(NativeLabPath(L"native-game-tiled-assets\\native_text_overlay.hlsl"),nullptr,"main",&b,&e))){if(e)e->Release();throw 3;}if(e)e->Release();
   D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={b->GetBufferPointer(),b->GetBufferSize()};HRESULT hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso));b->Release();if(FAILED(hr))throw 4;
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=UINT64(strip_pitch)*strip_h;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   if(FAILED(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&strip))))throw 5;
   return true;
  }catch(int step){failed=true;if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu text_overlay_unavailable step=%d\n",GetCurrentProcessId(),step);fclose(f);}return false;}
 }
public:
 /* build the pipeline ahead of time (from the frame before the network starts initializing, so no compile runs next to the initializer) */
 void Prepare(ID3D12Resource*texture){if(!texture)return;ID3D12Device*d=nullptr;if(FAILED(texture->GetDevice(IID_PPV_ARGS(&d))))return;Create(d);d->Release();}
 bool Ready()const{return device&&!failed;}
 ~NativeTextOverlay(){if(strip)strip->Release();if(pso)pso->Release();if(root)root->Release();if(device)device->Release();}
 /* text: ASCII, at most 64 chars (lower case is raised); drawn `scale` times enlarged at (x, y) of the host texture, which is in `state` */
 void Draw(ID3D12GraphicsCommandList*c,ID3D12Resource*texture,const char*text,UINT x=24,UINT y=24,UINT scale=3,D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS){
  if(!c||!texture||!text)return;auto desc=texture->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D)return;
  UINT mode=0,bpp=0;const DXGI_FORMAT cf=CopyFormat(desc.Format,mode,bpp);if(cf==DXGI_FORMAT_UNKNOWN)return;
  ID3D12Device*d=nullptr;if(FAILED(texture->GetDevice(IID_PPV_ARGS(&d))))return;const bool ok=Create(d);d->Release();if(!ok)return;
  UINT words[24]{};UINT n=0;for(;text[n]&&n<64;n++){unsigned char ch=(unsigned char)std::toupper((unsigned char)text[n]);if(ch<32||ch>=96)ch=' ';words[n/4]|=UINT(ch)<<((n%4)*8);}
  const UINT w=n*6*scale+2*scale,h=9*scale;if(!n||w>strip_w||h>strip_h||x+w>desc.Width||y+h>desc.Height)return;
  const UINT pitch=((w*bpp)+255)/256*256;words[16]=scale;words[17]=n;words[18]=mode;words[19]=pitch;
  D3D12_RESOURCE_BARRIER b[2]{};b[0].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b[0].Transition={strip,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
  b[1].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b[1].Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_DEST};
  if(!cached||std::memcmp(cached_words,words,sizeof words)){
   c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootUnorderedAccessView(0,strip->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(1,24,words,0);
   c->Dispatch((w+7)/8,(h+7)/8,1);
   std::memcpy(cached_words,words,sizeof words);cached=true;
  }
  c->ResourceBarrier(1,b);if(state!=D3D12_RESOURCE_STATE_COPY_DEST)c->ResourceBarrier(1,b+1);
  D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=strip;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint={cf,w,h,1,pitch};
  c->CopyTextureRegion(&dst,x,y,0,&src,nullptr);
  for(auto&v:b)std::swap(v.Transition.StateBefore,v.Transition.StateAfter);
  c->ResourceBarrier(1,b);if(state!=D3D12_RESOURCE_STATE_COPY_DEST)c->ResourceBarrier(1,b+1);
 }
};
