#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <fstream>
#include <cstdio>
#include <cmath>
#include "native_post70.h"
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("post test HRESULT="+std::to_string(unsigned(h)));}
static std::vector<float> read(const std::wstring&p,size_t count){std::ifstream f(p.c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(count*4))throw std::runtime_error("post fixture size");std::vector<float>v(count);f.seekg(0);if(!f.read((char*)v.data(),count*4))throw std::runtime_error("post fixture read");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("nonfinite input");return v;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&r)));return r;}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2&&argc!=3)return 2;bool game=argc==3&&!wcscmp(argv[2],L"game");if(argc==3&&!game)return 2;std::wstring dir=argv[1];auto file=[&](const wchar_t*n){return dir+L"\\"+n;};const UINT width=game?1920:512,height=game?1152:512,pixels=width*height;
 auto main=read(file(L"main.f32"),pixels*8),skip=read(file(L"skip.f32"),pixels*32),color=read(file(L"color.f32"),pixels*4),oracle=read(file(L"oracle.f32"),pixels*3);
 if(_wgetenv(L"DLSS5_POST_BASE_ONLY")){for(UINT i=0;i<pixels;i++)for(UINT c=0;c<3;c++)oracle[i*3+c]=color[i*4+c];std::fprintf(stderr,"base-only diagnostic; not neural acceptance\n");}
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=a;std::fwprintf(stderr,L"adapter=%ls\n",desc.Description);break;}a->Release();}if(!adapter)throw std::runtime_error("AMD missing");ID3D12Device*d=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
 auto upload=[&](const std::vector<float>&v){auto*r=buffer(d,v.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,v.data(),v.size()*4);r->Unmap(0,nullptr);return r;};
 auto*mi=upload(main);auto*si=upload(skip);auto*ci=upload(color);NativePost70 post;
 post.Create(d,mi,si,ci,width,height,read(file(L"scales.f32"),64),read(file(L"ffn.f32"),8736),read(file(L"attention.f32"),8225),read(file(L"head.f32"),96),dir);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*allocator=nullptr;ck(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ID3D12GraphicsCommandList*cmd=nullptr;ck(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&cmd)));ID3D12Fence*fence=nullptr;ck(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 const UINT64 bytes=oracle.size()*4;auto*rb=buffer(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);std::vector<float>baseline;
 for(UINT frame=0;frame<3;frame++){
  if(frame){ck(allocator->Reset());ck(cmd->Reset(allocator,nullptr));}post.Record(cmd);
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={post.Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};cmd->ResourceBarrier(1,&b);cmd->CopyBufferRegion(rb,0,post.Output(),0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);cmd->ResourceBarrier(1,&b);ck(cmd->Close());ID3D12CommandList*lists[]={cmd};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,frame+1));ck(fence->SetEventOnCompletion(frame+1,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("post GPU timeout");ck(d->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();if(done==UINT64_MAX||done<frame+1)throw std::runtime_error("post fence incomplete");
  void*p=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)},none{};ck(rb->Map(0,&range,&p));std::vector<float>result(oracle.size());std::memcpy(result.data(),p,bytes);rb->Unmap(0,&none);size_t different=0;float maximum=0;
  for(size_t i=0;i<result.size();i++){if(!std::isfinite(result[i]))throw std::runtime_error("post nonfinite");different+=result[i]!=oracle[i];maximum=std::max(maximum,std::abs(result[i]-oracle[i]));}
  std::ofstream out(file(_wgetenv(L"DLSS5_POST_BASE_ONLY")?L"gpu-base.f32":L"gpu.f32").c_str(),std::ios::binary);if(!out.write((char*)result.data(),bytes))throw std::runtime_error("post readback write");out.close();
  std::printf("frame=%u values=%zu different=%zu max_error=%.9g\n",frame,result.size(),different,maximum);std::fflush(stdout);
  if(different){
   // Failure-only readback after the submission fence; no CPU feature injection.
   UINT serial=100;
   for(auto item:std::vector<std::pair<ID3D12Resource*,const wchar_t*>>{{post.Merged(),L"gpu-merged.f32"},{post.Features(),L"gpu-features.f32"}}){
    UINT64 n=UINT64(pixels)*32*4;auto*diagnostic=buffer(d,n,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    ck(allocator->Reset());ck(cmd->Reset(allocator,nullptr));b.Transition={item.first,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};cmd->ResourceBarrier(1,&b);cmd->CopyBufferRegion(diagnostic,0,item.first,0,n);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);cmd->ResourceBarrier(1,&b);ck(cmd->Close());ID3D12CommandList*dl[]={cmd};q->ExecuteCommandLists(1,dl);ck(q->Signal(fence,serial));ck(fence->SetEventOnCompletion(serial,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("diagnostic timeout");ck(d->GetDeviceRemovedReason());if(fence->GetCompletedValue()==UINT64_MAX||fence->GetCompletedValue()<serial)throw std::runtime_error("diagnostic fence");serial++;
    D3D12_RANGE dr{0,SIZE_T(n)};ck(diagnostic->Map(0,&dr,&p));std::ofstream f(file(item.second).c_str(),std::ios::binary);if(!f.write((char*)p,n))throw std::runtime_error("diagnostic write");diagnostic->Unmap(0,&none);diagnostic->Release();
   }
   throw std::runtime_error("original post RGB mismatch");
  }
  if(!frame)baseline=result;else if(result!=baseline)throw std::runtime_error("post replay differs");
 }
 std::puts(_wgetenv(L"DLSS5_POST_BASE_ONLY")?"post70_base_only=exact NOT_neural_acceptance":"post70=exact frames=3 intermediate_CPU_transfers=0");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
