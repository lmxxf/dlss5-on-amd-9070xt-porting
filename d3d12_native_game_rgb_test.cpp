#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cmath>
#include "native_game_rgb_input.h"
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("texture test HRESULT="+std::to_string(unsigned(h)));}
static ID3D12Resource*buf(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
 D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=bytes;r.Height=1;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*out=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&out)));return out;
}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 info{};a->GetDesc1(&info);if(info.VendorId==0x1002){ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();break;}a->Release();}if(!d)throw std::runtime_error("AMD missing");
 D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1920;td.Height=1080;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ID3D12Resource*texture=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 upload_bytes=0;d->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&upload_bytes);
 auto*upload=buf(d,upload_bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
 const UINT64 bytes=1920ull*1152*16;auto*rb=buf(d,bytes*2,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 NativeGameRgbInput bridge;bridge.Create(d,texture,argv[1]);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*alloc=nullptr;ck(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));ID3D12GraphicsCommandList*c=nullptr;ck(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_PPV_ARGS(&c)));ID3D12Fence*fence=nullptr;ck(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");
 auto value=[](UINT x,UINT y,UINT channel,bool alternate){return float(int((x*13+y*7+channel*311+(alternate?997:0))%4096)-1024)/1024.f;};
 for(UINT frame=0;frame<5;frame++){
  if(frame){ck(alloc->Reset());ck(c->Reset(alloc,nullptr));}bool alternate=frame==2;void*p=nullptr;D3D12_RANGE none{};ck(upload->Map(0,&none,&p));
  for(UINT y=0;y<1080;y++){auto*row=reinterpret_cast<float*>(static_cast<unsigned char*>(p)+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch);for(UINT x=0;x<1920;x++)for(UINT ch=0;ch<4;ch++)row[x*4+ch]=value(x,y,ch,alternate);}upload->Unmap(0,nullptr);
  D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  bridge.Record(c,D3D12_RESOURCE_STATE_COPY_DEST);
  ID3D12Resource*outputs[]={bridge.Tiles(),bridge.PostBase()};
  for(UINT i=0;i<2;i++){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={outputs[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,bytes*i,outputs[i],0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);}
  ck(c->Close());ID3D12CommandList*lists[]={c};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,frame+1));ck(fence->SetEventOnCompletion(frame+1,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("texture GPU timeout");ck(d->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();if(done==UINT64_MAX||done<frame+1)throw std::runtime_error("invalid completion");
  D3D12_RANGE range{0,SIZE_T(bytes*2)};ck(rb->Map(0,&range,&p));auto*actual=static_cast<float*>(p);size_t differences[2]{};
  for(UINT y=0;y<1152;y++)for(UINT x=0;x<1920;x++)for(UINT ch=0;ch<4;ch++){
   // Independently reflect coordinates using the periodic definition.
   UINT r=y%(2*1079),sy=r<1080?r:2158-r;float expected=value(x,sy,ch,alternate);
   size_t tile=((size_t(y/8)*240+x/8)*64+(y%8)*8+x%8)*4+ch;
   size_t linear=(size_t(y)*1920+x)*4+ch;
   differences[0]+=actual[tile]!=expected;differences[1]+=actual[bytes/4+linear]!=expected;
  }
  rb->Unmap(0,&none);std::printf("frame=%u input=%c tile_different=%zu base_different=%zu values_each=8847360\n",frame,alternate?'B':'A',differences[0],differences[1]);std::fflush(stdout);if(differences[0]||differences[1])throw std::runtime_error("texture pixels differ");
 }
 std::puts("texture_input=exact float32 A/A/B/A/A; actual game integration pending");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
