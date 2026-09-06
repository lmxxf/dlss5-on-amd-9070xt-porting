#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <fstream>
#include <cstdio>
#include <cmath>
#include "native_vit_attention.h"
#include "native_vit_qkv.h"
static void ck(HRESULT hr){if(FAILED(hr))throw std::runtime_error("HRESULT="+std::to_string(unsigned(hr)));}
static std::vector<float> read(const std::wstring& path){
 std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing fixture");
 auto size=f.tellg();if(size<=0||size_t(size)%4)throw std::runtime_error("fixture size");
 std::vector<float> v(size_t(size)/4);f.seekg(0);if(!f.read((char*)v.data(),size))throw std::runtime_error("short read");return v;
}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
 D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
 desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2&&argc!=3)return 2;bool qkv_mode=argc==3&&!wcscmp(argv[2],L"qkv"),chain_mode=argc==3&&!wcscmp(argv[2],L"qkv_attention");if(argc==3&&!qkv_mode&&!chain_mode)return 2;
 std::wstring dir=argv[1];auto values=read(dir+L"\\input.f32"),oracle=read(dir+L"\\oracle.f32");
 UINT tokens=UINT((qkv_mode?values.size():oracle.size())/1024);
 if(chain_mode?values.size()!=oracle.size():(qkv_mode?(values.size()!=size_t(tokens)*1024||oracle.size()!=values.size()*3):(oracle.size()!=size_t(tokens)*1024||values.size()!=oracle.size()*3)))throw std::runtime_error("geometry");
 for(float v:values)if(!std::isfinite(v))throw std::runtime_error("nonfinite input");
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;
  DXGI_ADAPTER_DESC1 desc{};ck(a->GetDesc1(&desc));if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=a;std::wprintf(L"adapter=%ls\n",desc.Description);break;}a->Release();}
 if(!adapter)throw std::runtime_error("AMD missing");ID3D12Device*d=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
 auto*input=buffer(d,values.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*m=nullptr;D3D12_RANGE empty{};
 ck(input->Map(0,&empty,&m));std::memcpy(m,values.data(),values.size()*4);input->Unmap(0,nullptr);
 NativeVitAttention attention;NativeVitQkv qkv;
 if(qkv_mode||chain_mode)qkv.Create(d,input,tokens,read(dir+L"\\weights.f32"),dir);
 if(!qkv_mode)attention.Create(d,chain_mode?qkv.Output():input,tokens,dir);
 auto*output=qkv_mode?qkv.Output():attention.Output();
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
 ID3D12CommandAllocator*alloc=nullptr;ck(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
 ID3D12GraphicsCommandList*cmd=nullptr;ck(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_PPV_ARGS(&cmd)));
 ID3D12Fence*fence=nullptr;ck(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");
 UINT64 bytes=oracle.size()*4;auto*rb=buffer(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);std::vector<float> result(oracle.size());
 for(UINT frame=0;frame<3;frame++){
  if(frame){ck(alloc->Reset());ck(cmd->Reset(alloc,nullptr));}if(qkv_mode||chain_mode)qkv.Record(cmd);if(!qkv_mode)attention.Record(cmd);
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
  cmd->ResourceBarrier(1,&b);cmd->CopyBufferRegion(rb,0,output,0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);cmd->ResourceBarrier(1,&b);ck(cmd->Close());
  ID3D12CommandList*lists[]={cmd};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,frame+1));ck(fence->SetEventOnCompletion(frame+1,event));
  if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");ck(d->GetDeviceRemovedReason());auto done=fence->GetCompletedValue();if(done==UINT64_MAX||done<frame+1)throw std::runtime_error("fence");
  D3D12_RANGE range{0,SIZE_T(bytes)};ck(rb->Map(0,&range,&m));std::memcpy(result.data(),m,bytes);rb->Unmap(0,&empty);
  size_t diff=0;float maximum=0;for(size_t i=0;i<result.size();i++){if(!std::isfinite(result[i])||!std::isfinite(oracle[i]))throw std::runtime_error("nonfinite");diff+=result[i]!=oracle[i];maximum=std::max(maximum,std::abs(result[i]-oracle[i]));}
  std::printf("tokens=%u frame=%u values=%zu different=%zu max_abs=%.9g\n",tokens,frame,result.size(),diff,maximum);std::fflush(stdout);
  std::ofstream out((dir+L"\\gpu.f32").c_str(),std::ios::binary);if(!out.write((char*)result.data(),bytes))throw std::runtime_error("write");
  if(diff)throw std::runtime_error("original mismatch");
 }
 return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
