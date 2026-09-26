// Import/map/release D3D12 shared buffers into HIP exactly like hip_d3d12_bridge.h Share/Release, cycling sizes.
#include "hip_api.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <cstdio>
using namespace hip_probe;
static void C(HRESULT h,const char*w){if(FAILED(h)){std::fprintf(stderr,"FAIL %s %08lx\n",w,(unsigned long)h);std::exit(1);}}
struct Shared{ID3D12Resource*r{};HANDLE h{};Handle imp{};void*map{};};
int main(int argc,char**argv){
 const int N=argc>1?atoi(argv[1]):40;const int mode=argc>2?atoi(argv[2]):0; // 0 = bridge order (hipFree mapped, destroy, close, release); 1 = skip hipFree
 IDXGIFactory4*f{};C(CreateDXGIFactory1(IID_PPV_ARGS(&f)),"factory");IDXGIAdapter1*ad{};ID3D12Device*dev{};
 for(UINT i=0;f->EnumAdapters1(i,&ad)!=DXGI_ERROR_NOT_FOUND;i++){DXGI_ADAPTER_DESC1 d{};ad->GetDesc1(&d);if(d.VendorId==0x1002&&SUCCEEDED(D3D12CreateDevice(ad,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dev))))break;ad->Release();ad=nullptr;}
 IDXGIAdapter3*a3{};ad->QueryInterface(IID_PPV_ARGS(&a3));
 Api api(7);api.Load(api.hipSetDevice,"hipSetDevice");api.Load(api.hipMemGetInfo,"hipMemGetInfo");api.Load(api.hipFree,"hipFree");api.Load(api.hipMemsetAsync,"hipMemsetAsync");api.Load(api.hipDeviceSynchronize,"hipDeviceSynchronize");
 api.Load(api.hipImportExternalMemory,"hipImportExternalMemory");api.Load(api.hipExternalMemoryGetMappedBuffer,"hipExternalMemoryGetMappedBuffer");api.Load(api.hipDestroyExternalMemory,"hipDestroyExternalMemory");api.hipGetErrorName=reinterpret_cast<Api::ErrorNameFn>(GetProcAddress(api.dll,"hipGetErrorName"));
 api.Check(api.hipInit(0),"init");api.Check(api.hipSetDevice(0),"setdev");
 auto report=[&](int it,unsigned w,unsigned hgt){DXGI_QUERY_VIDEO_MEMORY_INFO vi{};a3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&vi);PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof pm;GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&pm,sizeof pm);size_t fr=0,tot=0;api.hipMemGetInfo(&fr,&tot);
  std::printf("it=%d size=%ux%u dxgi_local_MiB=%.1f private_MiB=%.1f hip_free_MiB=%.1f\n",it,w,hgt,vi.CurrentUsage/1048576.,pm.PrivateUsage/1048576.,fr/1048576.);std::fflush(stdout);};
 auto share=[&](Shared&s,size_t bytes,bool uav){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
  C(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&s.r)),"buf");C(dev->CreateSharedHandle(s.r,nullptr,GENERIC_ALL,nullptr,&s.h),"handle");
  MemoryDesc md{};md.type=5;md.handle.win32.handle=s.h;md.size=dev->GetResourceAllocationInfo(0,1,&rd).SizeInBytes;md.flags=1;api.Check(api.hipImportExternalMemory(&s.imp,&md),"import");BufferDesc bd{};bd.size=bytes;api.Check(api.hipExternalMemoryGetMappedBuffer(&s.map,s.imp,&bd),"map");
  api.Check(api.hipMemsetAsync(s.map,0,bytes,nullptr),"touch");};
 auto release=[&](Shared&s){int a=0,b=0;if(s.map&&mode==0)a=api.hipFree(s.map);if(s.imp)b=api.hipDestroyExternalMemory(s.imp);if(s.h)CloseHandle(s.h);if(s.r)s.r->Release();if(a||b)std::printf("  release rc hipFree=%d destroy=%d\n",a,b);s={};};
 const unsigned sizes[][2]={{1920,1152},{1600,960},{1280,768},{1920,1152}};
 report(-1,0,0);
 for(int it=0;it<N;it++){auto&sz=sizes[it%4];size_t px=size_t(sz[0])*sz[1];Shared in,hi,out;share(in,px*16,false);share(hi,px*16,false);share(out,px*12,true);api.Check(api.hipDeviceSynchronize(),"sync");release(in);release(hi);release(out);report(it,sz[0],sz[1]);}
 return 0;}
