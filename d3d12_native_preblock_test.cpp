#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <cstdint>
#include "native_preblock_runtime.h"
#include "native_rgb_reflect.h"
#include "native_c32_stage.h"
#include "native_c32_ds.h"
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D HRESULT="+std::to_string(unsigned(h)));}
static std::vector<float> read(const wchar_t*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("input file missing");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("input size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("input truncated");return v;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 n,D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES s){D3D12_HEAP_PROPERTIES hp{};hp.Type=t;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=n;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*r=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,s,nullptr,IID_PPV_ARGS(&r)));return r;}
static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);}
int wmain(int argc,wchar_t**argv){try{
 if(argc<8||argc>10){std::fwprintf(stderr,L"usage: %ls ffn.f32 attention.f32 input.rgba32f main.f32 down.f32 raw.f32 shader-dir [live [global]]\n",argv[0]);return 2;}
 const bool live=argc>=9&&!wcscmp(argv[8],L"live"),raw_features=argc>=9&&!wcscmp(argv[8],L"c32");if(argc>=9&&!live&&!raw_features)return 2;
 const bool global=argc==10&&!wcscmp(argv[9],L"global");if(argc==10&&!global)return 2;
 auto fw=read(argv[1]),aw=read(argv[2]),rgb=read(argv[3]);const UINT width=std::getenv("DLSS5_TEST_WIDTH")?std::atoi(std::getenv("DLSS5_TEST_WIDTH")):128,channels=raw_features?32:4;if(!width)throw std::runtime_error("width zero");UINT height=UINT(rgb.size()/channels/width);if(rgb.size()!=size_t(width)*height*channels||height%8)throw std::runtime_error("fixture geometry");
 const bool reflect_input=_wgetenv(L"DLSS5_REFLECT_VALID1080")!=nullptr;if(reflect_input){if(!live||!global||width!=1920||height!=1080)throw std::runtime_error("reflect test contract");height=1152;}
 IDXGIFactory6*factory=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));IDXGIAdapter1*adapter=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};c->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=c;std::wprintf(L"adapter=%ls\n",desc.Description);break;}c->Release();}
 if(!adapter)throw std::runtime_error("AMD adapter unavailable");ID3D12Device*device=nullptr;ck(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
 auto*input=buffer(device,rgb.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*m=nullptr;D3D12_RANGE none{};ck(input->Map(0,&none,&m));std::memcpy(m,rgb.data(),rgb.size()*4);input->Unmap(0,nullptr);
 std::vector<float>noise_table;const wchar_t*noise_path=_wgetenv(L"DLSS5_NOISE_TABLE");if(noise_path)noise_table=read(noise_path);
 NativeRgbReflect reflect;if(reflect_input){if(!noise_path)throw std::runtime_error("reflect test requires native noise table");reflect.Create(device,input,1920,1080,1920,1152,argv[7]);}
 NativePreblockRuntime block;block.Create(device,reflect_input?reflect.Output():input,width,height,fw,aw,argv[7],live,raw_features,noise_path?&noise_table:nullptr);
 const bool front4=_wgetenv(L"DLSS5_FRONT4")!=nullptr;if(front4&&!reflect_input)throw std::runtime_error("front4 requires reflected valid RGB");NativeC32Stage stages[4];NativeC32Downsample ds;
 if(front4){auto*source=block.Downsample();const UINT shifts[]={0,3,1,2};for(UINT i=0;i<4;i++){auto prefix=std::wstring(argv[7])+L"\\block"+std::to_wstring(i+1);stages[i].Create(device,source,960,576,shifts[i],read((prefix+L"-ffn.f32").c_str()),read((prefix+L"-attention.f32").c_str()),argv[7]);source=stages[i].Output();}ds.Create(device,stages[3].PooledWork(),960,576,2,read((std::wstring(argv[7])+L"\\block4-ds.f32").c_str()),argv[7]);}
 D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*q=nullptr;ck(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*allocator=nullptr;ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));ID3D12GraphicsCommandList*c=nullptr;ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&c)));
 ID3D12Fence*fence=nullptr;ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 UINT64 sizes[]={UINT64(width)*height*32*4,UINT64(width)*height*8*4,UINT64(width)*height*32*4};ID3D12Resource*outputs[]={front4?stages[3].Output():block.Main(),front4?ds.Output():block.Downsample(),block.RawTiles()};if(front4){sizes[0]=UINT64(960)*576*32*4;sizes[1]=UINT64(480)*288*64*4;}ID3D12Resource*readback[3]{};std::vector<float>data[3],baseline[3];for(UINT i=0;i<3;i++){readback[i]=buffer(device,sizes[i],D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);data[i].resize(sizes[i]/4);}
 const UINT base_seed=live?(std::getenv("DLSS5_TEST_SEED")?UINT(std::strtoul(std::getenv("DLSS5_TEST_SEED"),nullptr,0)):0):0x3f800000;
 for(UINT frame=0;frame<5;frame++){
  if(frame){ck(allocator->Reset());ck(c->Reset(allocator,nullptr));}
  UINT seed=live?(base_seed^(frame==3?1u:0u)):(frame==3?0x12345678:base_seed);if(reflect_input)reflect.Record(c);block.Record(c,seed,!global);if(front4){for(auto&stage:stages)stage.Record(c);ds.Record(c);}
  for(UINT i=0;i<3;i++){barrier(c,outputs[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);c->CopyBufferRegion(readback[i],0,outputs[i],0,sizes[i]);barrier(c,outputs[i],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
  ck(c->Close());ID3D12CommandList*lists[]={c};q->ExecuteCommandLists(1,lists);ck(q->Signal(fence,frame+1));ck(fence->SetEventOnCompletion(frame+1,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
  ck(device->GetDeviceRemovedReason());const auto completed=fence->GetCompletedValue();if(completed==UINT64_MAX||completed<frame+1)throw std::runtime_error("invalid fence completion");
  for(UINT i=0;i<3;i++){D3D12_RANGE range{0,SIZE_T(sizes[i])};ck(readback[i]->Map(0,&range,&m));std::memcpy(data[i].data(),m,sizes[i]);readback[i]->Unmap(0,&none);for(float x:data[i])if(!std::isfinite(x))throw std::runtime_error("nonfinite output");}
  if(frame==0){for(UINT i=0;i<3;i++)baseline[i]=data[i];}
  else if(frame==3&&!raw_features){if(data[0]==baseline[0])throw std::runtime_error("seed has no effect");}
  else for(UINT i=0;i<3;i++)if(data[i]!=baseline[i])throw std::runtime_error("persistent-resource replay changed output");
  std::printf("frame=%u seed=%08x replay_check=pass\n",frame,seed);
 }
 for(UINT i=0;i<3;i++){std::ofstream f(argv[4+i],std::ios::binary);if(!f.write(reinterpret_cast<const char*>(data[i].data()),sizes[i]))throw std::runtime_error("output write failed");}
 std::printf("resident_chain=pass width=%u height=%u frames=5 intermediate_CPU_transfers=0\n",width,height);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
