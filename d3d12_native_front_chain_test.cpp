#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <fstream>
#include <cstdio>
#include <cmath>
#include "native_c32_stage.h"
#include "native_c32_ds.h"
#include "native_c64.h"
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D HRESULT="+std::to_string(unsigned(h)));}
static std::vector<float> read(const std::wstring&p){std::ifstream f(p.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing input");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("input size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("truncated input");return v;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 n,D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES s){D3D12_HEAP_PROPERTIES h{};h.Type=t;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=n;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&rd,s,nullptr,IID_PPV_ARGS(&r)));return r;}
static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);}
int wmain(int argc,wchar_t**argv){try{
 if(argc<2||argc>3)return 2;const bool with_c64=argc==3&&!wcscmp(argv[2],L"c64"),with_ds=with_c64||(argc==3&&!wcscmp(argv[2],L"ds"));if(argc==3&&!with_ds)return 2;std::wstring dir=argv[1];auto file=[&](const wchar_t*n){return dir+L"\\"+n;};auto rgb=read(file(L"input.rgba32f"));if(rgb.size()!=128*64*4)throw std::runtime_error("fixture shape");
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;for(UINT i=0;;i++){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};c->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=c;std::wprintf(L"adapter=%ls\n",desc.Description);break;}c->Release();}if(!adapter)throw std::runtime_error("AMD missing");ID3D12Device*device=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
 auto*input=buffer(device,rgb.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*m=nullptr;D3D12_RANGE none{};ck(input->Map(0,&none,&m));std::memcpy(m,rgb.data(),rgb.size()*4);input->Unmap(0,nullptr);
 NativePreblockRuntime pre;pre.Create(device,input,128,64,read(file(L"block0-ffn.f32")),read(file(L"block0-attention.f32")),dir,true);
 NativeC32Stage stages[4];UINT shifts[]={0,3,1,2};UINT stage_count=with_ds?4:3;ID3D12Resource*source=pre.Downsample();
 for(UINT i=0;i<stage_count;i++){auto prefix=L"block"+std::to_wstring(i+1);stages[i].Create(device,source,64,32,shifts[i],read(file((prefix+L"-ffn.f32").c_str())),read(file((prefix+L"-attention.f32").c_str())),dir);source=stages[i].Output();}
 NativeC32Downsample ds;if(with_ds){ds.Create(device,stages[3].PooledWork(),64,32,2,read(file(L"block4-ds.f32")),dir);source=ds.Output();}
 NativeC64 c64;if(with_c64){c64.Create(device,source,32,16,read(file(L"block5-ffn.f32")),read(file(L"block5-attention.f32")),dir);source=c64.Output();}
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*allocator=nullptr;ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ID3D12GraphicsCommandList*c=nullptr;ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&c)));ID3D12Fence*fence=nullptr;ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 const UINT64 bytes=with_ds?32*16*64*4:64*32*32*4;auto*rb=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);std::vector<float>result(bytes/4),baseline;
 for(UINT frame=0;frame<3;frame++){
  if(frame){ck(allocator->Reset());ck(c->Reset(allocator,nullptr));}pre.Record(c,0,false);for(UINT i=0;i<stage_count;i++)stages[i].Record(c);if(with_ds)ds.Record(c);if(with_c64)c64.Record(c);
  barrier(c,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);c->CopyBufferRegion(rb,0,source,0,bytes);barrier(c,source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);ck(c->Close());ID3D12CommandList*lists[]={c};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,frame+1));ck(fence->SetEventOnCompletion(frame+1,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");D3D12_RANGE all{0,bytes};ck(rb->Map(0,&all,&m));std::memcpy(result.data(),m,bytes);rb->Unmap(0,&none);for(float v:result)if(!std::isfinite(v))throw std::runtime_error("nonfinite result");if(frame==0)baseline=result;else if(result!=baseline)throw std::runtime_error("replay mismatch");std::printf("frame=%u replay=pass\n",frame);
 }
 std::ofstream out(file(with_c64?L"output-c64.f32":with_ds?L"output-ds.f32":L"output.f32").c_str(),std::ios::binary);if(!out.write(reinterpret_cast<const char*>(result.data()),bytes))throw std::runtime_error("output write");std::printf("native_blocks0_%u=executed intermediate_cpu_transfers=0\n",with_c64?5:stage_count);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
