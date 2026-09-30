// Standalone synthetic GPU regression; no game captures, model weights or HIP required.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "native_game_codec.h"
#include "native_game_submission.h"
using Microsoft::WRL::ComPtr;
static constexpr auto Read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
static void Check(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("HRESULT="+std::to_string(unsigned(hr))); }
static void Require(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
static void Barrier(ID3D12GraphicsCommandList *c,ID3D12Resource *r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
 if(a==b)return; D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,0,a,b};c->ResourceBarrier(1,&v);
}
struct Harness {
 ComPtr<ID3D12Device> d; ComPtr<ID3D12CommandQueue> q; NativeGameSubmission submit;
 explicit Harness(bool hardware) {
  ComPtr<ID3D12Debug> debug;
  if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) { debug->EnableDebugLayer(); puts("debug_layer=enabled"); }
  else puts("debug_layer=unavailable");
  ComPtr<IDXGIFactory6> f;Check(CreateDXGIFactory1(IID_PPV_ARGS(&f)));ComPtr<IDXGIAdapter1> a;
  if(!hardware)Check(f->EnumWarpAdapter(IID_PPV_ARGS(&a)));
  else for(UINT i=0;;++i) { Check(f->EnumAdapters1(i,&a));DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)break;a.Reset(); }
  Check(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));
  D3D12_COMMAND_QUEUE_DESC qd{};Check(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));submit.Create(q.Get(),false);
 }
 ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC rd{};
  rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> r;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;
 }
 ComPtr<ID3D12Resource> Texture(UINT w,UINT h,DXGI_FORMAT fmt,const std::vector<unsigned char>&data) {
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};
  rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=w;rd.Height=h;rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Format=fmt;
  ComPtr<ID3D12Resource> r;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size,row;d->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,&row,&size);
  Require(data.size()==row*h,"upload payload size");auto up=Buffer(size,D3D12_HEAP_TYPE_UPLOAD);void *p;D3D12_RANGE no{};Check(up->Map(0,&no,&p));
  for(UINT y=0;y<h;++y)memcpy(static_cast<char*>(p)+y*fp.Footprint.RowPitch,data.data()+y*row,size_t(row));up->Unmap(0,nullptr);
  submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=up.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;dst.pResource=r.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(c,r.Get(),D3D12_RESOURCE_STATE_COPY_DEST,Read);});return r;
 }
 std::vector<unsigned char> ReadTexture(ID3D12Resource *r) {
  auto rd=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size,row;d->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,&row,&size);auto rb=Buffer(size,D3D12_HEAP_TYPE_READBACK);
  submit.Submit([&](ID3D12GraphicsCommandList*c){Barrier(c,r,Read,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(c,r,D3D12_RESOURCE_STATE_COPY_SOURCE,Read);});
  void*p;D3D12_RANGE range{0,size_t(size)};Check(rb->Map(0,&range,&p));std::vector<unsigned char> result(size_t(row)*rd.Height);for(UINT y=0;y<rd.Height;++y)memcpy(result.data()+y*row,static_cast<char*>(p)+y*fp.Footprint.RowPitch,size_t(row));D3D12_RANGE no{};rb->Unmap(0,&no);return result;
 }
 std::vector<unsigned char> Run(NativeGameCodec&codec,NativeCodecParameters p,UINT inputs,ID3D12Resource *writeback=nullptr,float white=1) {
  submit.Submit([&](ID3D12GraphicsCommandList*c){codec.Record(c,std::vector<D3D12_RESOURCE_STATES>(inputs,Read),white,p);
   if(writeback){Require(codec.BufferOutput(),"packed output route selected");Barrier(c,codec.Output(),Read,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(c,writeback,Read,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=codec.Output();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint.Footprint=codec.BufferFootprint();dst.pResource=writeback;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(c,codec.Output(),D3D12_RESOURCE_STATE_COPY_SOURCE,Read);Barrier(c,writeback,D3D12_RESOURCE_STATE_COPY_DEST,Read);}
  });return ReadTexture(writeback?writeback:codec.Output());
 }
 void CheckDebug() {
  ComPtr<ID3D12InfoQueue> info;if(FAILED(d.As(&info)))return;
  for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<char>b(n);auto*m=reinterpret_cast<D3D12_MESSAGE*>(b.data());Check(info->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)throw std::runtime_error(m->pDescription);}
 }
};
static std::vector<unsigned char> Half(UINT w,UINT h,unsigned kind) {
 std::vector<unsigned char>b(size_t(w)*h*8);const uint16_t colors[3][4]={{0x3400,0x3800,0x3000,0x3c00},{0x3a00,0x3400,0x3800,0x3c00},{0x3800,0x3000,0x3400,0x3c00}};
 for(size_t i=0;i<size_t(w)*h;++i)memcpy(b.data()+i*8,colors[kind],8);return b;
}
int wmain(int argc,wchar_t**argv) { try {
 Require(argc>=3,"usage: test shaders legacy-shaders [amd]");_wputenv_s(L"DLSS5_SHADER_DISK_CACHE",L"0");_wputenv_s(L"DLSS5_NETWORK_HEIGHT",L"1080");Harness h(argc>3);const std::wstring shaders=argv[1],legacy=argv[2];
 NativeCodecParameters valid;valid.debug_view=static_cast<NativeCodecDebugView>(0x10001);Require(!valid.Valid(),"reject flags injected into debug enum");
 NativeResolveNetworkGeometry(65,7);auto geo=NativeCurrentNetworkGeometry();
 auto proxy=h.Texture(geo.valid_width,geo.valid_height,DXGI_FORMAT_R16G16B16A16_FLOAT,Half(geo.valid_width,geo.valid_height,0));
 auto neural=h.Texture(geo.valid_width,geo.valid_height,DXGI_FORMAT_R16G16B16A16_FLOAT,Half(geo.valid_width,geo.valid_height,1));
 auto original=h.Texture(65,7,DXGI_FORMAT_R16G16B16A16_FLOAT,Half(65,7,2));
 for(int srgb=0;srgb<2;++srgb){_wputenv_s(L"DLSS5_CODEC_SRGB",srgb?L"1":L"0");
  NativeGameCodec now,old,enc,oldEnc;now.Create(h.d.Get(),{proxy.Get(),neural.Get(),original.Get()},shaders);old.Create(h.d.Get(),{proxy.Get(),neural.Get(),original.Get()},legacy);
  enc.Create(h.d.Get(),{original.Get()},shaders);oldEnc.Create(h.d.Get(),{original.Get()},legacy);
  for(float cs:{0.f,.5f,1.f,2.f,3.f}){NativeCodecParameters p;p.color_strength=cs;p.pre_exposure=2;
   Require(h.Run(now,p,3)==h.Run(old,p,3),"legacy default decoder bytes unchanged");}
  NativeCodecParameters p;p.pre_exposure=2;auto base=h.Run(enc,p,1);Require(base==h.Run(oldEnc,p,1),"legacy default encoder unchanged with preExposure=2");
  p.use_pre_exposure=true;auto opted=h.Run(enc,p,1);Require(srgb?opted==base:opted!=base,"pre-exposure opt-in encoder behavior");
  if(!srgb) {
   NativeCodecParameters automatic;automatic.auto_white=true;
   auto autoEncoded=h.Run(enc,automatic,1),autoDecoded=h.Run(now,automatic,3);
   automatic.use_pre_exposure=true;automatic.pre_exposure=2;
   Require(autoEncoded==h.Run(enc,automatic,1)&&autoDecoded==h.Run(now,automatic,3),"auto white precedes pre-exposure-only");
  }
  if(!srgb){auto decoded=h.Run(now,p,3);p.use_pre_exposure=false;Require(decoded!=h.Run(now,p,3),"pre-exposure opt-in decoder behavior");
   auto oldDefault=h.Run(now,p,3);p.hue_safe=true;Require(oldDefault!=h.Run(now,p,3),"hue-safe opt-in changes colored fixture");p.color_strength=2;Require(oldDefault==h.Run(now,p,3),"hue-safe CS2 equals legacy CS1");}
  for(DXGI_FORMAT format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R16G16B16A16_UNORM}) {
   const UINT bpp=format==DXGI_FORMAT_R16G16B16A16_UNORM?8:4;std::vector<unsigned char> pattern(65*7*bpp);
   for(size_t i=0;i<pattern.size()/4;++i){const uint32_t v=format==DXGI_FORMAT_R11G11B10_FLOAT?0x641e03c0u:0xc0408020u+UINT(i%31);memcpy(pattern.data()+i*4,&v,4);}
   auto src=h.Texture(65,7,format,pattern),target=h.Texture(65,7,format,pattern);
   NativeGameCodec currentPacked,oldPacked;currentPacked.Create(h.d.Get(),{proxy.Get(),neural.Get(),src.Get()},shaders);oldPacked.Create(h.d.Get(),{proxy.Get(),neural.Get(),src.Get()},legacy);
   NativeCodecParameters p;p.pre_exposure=2;
   Require(h.Run(currentPacked,p,3,target.Get())==h.Run(oldPacked,p,3,target.Get()),"legacy packed output unchanged (RGBA8/BGRA8/R11/UNORM16)");
  }
  if(srgb)for(UINT view=1;view<=4;++view){NativeCodecParameters p;p.debug_view=static_cast<NativeCodecDebugView>(view);auto baseline=h.Run(now,p,3);p.auto_white=true;p.use_pre_exposure=true;Require(baseline==h.Run(now,p,3),"debug views survive high flags");}
 }
 _wputenv_s(L"DLSS5_CODEC_SRGB",L"0");
 for(UINT width:{65u,1920u}){const UINT height=width==65?7:1080;std::vector<unsigned char>pattern(size_t(width)*height*4);const UINT levels[]={0,1,2,511,512,1022,1023};
  for(size_t i=0;i<pattern.size()/4;++i){uint32_t v=levels[i%7]|(levels[(i+2)%7]<<10)|(levels[(i+4)%7]<<20)|(UINT(i%4)<<30);memcpy(pattern.data()+i*4,&v,4);}
  auto r10=h.Texture(width,height,DXGI_FORMAT_R10G10B10A2_UNORM,pattern);auto target=h.Texture(width,height,DXGI_FORMAT_R10G10B10A2_UNORM,pattern);
  NativeGameCodec dec;dec.Create(h.d.Get(),{proxy.Get(),neural.Get(),r10.Get()},shaders);Require(dec.BufferOutput()&&dec.BufferFootprint().Format==DXGI_FORMAT_R10G10B10A2_UNORM&&dec.BufferFootprint().RowPitch==((width*4+255)&~255u),"R10 format and row pitch");
  NativeCodecParameters p;p.transfer_strength=0;p.color_strength=0;Require(h.Run(dec,p,3,target.Get())==pattern,"R10 packed texture round-trip including alpha and boundaries");
  auto other=pattern;for(size_t i=0;i<other.size();++i)other[i]^=0x55;auto alternate=h.Texture(width,height,DXGI_FORMAT_R10G10B10A2_UNORM,other);dec.RebindInputAfterCompletion(2,alternate.Get());Require(h.Run(dec,p,3,target.Get())==other,"R10 rebind UAV route round-trip");
  NativeGameCodec fp;fp.Create(h.d.Get(),{proxy.Get(),neural.Get(),r10.Get()},shaders,true);Require(!fp.BufferOutput()&&fp.Output()->GetDesc().Format==DXGI_FORMAT_R16G16B16A16_FLOAT,"R10 private FP16 route retained");h.Run(fp,p,3);
 }
 h.CheckDebug();puts("PASS: legacy HDR/sRGB bytes, opt-ins, four debug views, R10 real texture write-back/rebind/alpha/row-pitch/private-FP16");return 0;
 }catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
