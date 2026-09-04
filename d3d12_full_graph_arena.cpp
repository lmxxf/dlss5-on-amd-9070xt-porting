#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <cstdio>
#include <vector>
static void ck(const char*n,HRESULT h){if(FAILED(h)){std::fprintf(stderr,"%s=0x%08lx\n",n,h);ExitProcess(1);}}
static D3D12_HEAP_PROPERTIES hp(){D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;h.CreationNodeMask=h.VisibleNodeMask=1;return h;}
static D3D12_RESOURCE_DESC bd(UINT64 n){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=n;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;return d;}
int main(){
 IDXGIFactory6*f=nullptr;ck("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));IDXGIAdapter1*a=nullptr;DXGI_ADAPTER_DESC1 ad{};for(UINT i=0;;i++){IDXGIAdapter1*x=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&x))==DXGI_ERROR_NOT_FOUND)break;x->GetDesc1(&ad);if(!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(ad.Description,L"AMD")){a=x;break;}x->Release();}if(!a)return 2;ID3D12Device*d=nullptr;ck("device",D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));wprintf(L"adapter=%ls dedicated_mib=%llu\n",ad.Description,(unsigned long long)(ad.DedicatedVideoMemory>>20));
 struct Spec{const char*name;UINT64 bytes;};constexpr UINT64 MiB=1ull<<20;const Spec specs[]={
  {"skip_block0",1920ull*1088*32*4},{"skip_block4",1920ull*1088*32*4},
  {"skip_block8",960ull*544*64*4},{"skip_block14",480ull*272*128*4},
  {"skip_block22",240ull*136*256*4},{"skip_block30",120ull*72*512*4},
  {"main_ping",1920ull*1088*32*4},{"main_pong",1920ull*1088*32*4},
  {"swin_feature",1920ull*1088*32*4},{"swin_qkv_max",1920ull*1088*48*4},
  {"swin_attention_max",960ull*544*32*4},
  {"vit_branch",2160ull*4096*4},{"vit_hidden",2160ull*1024*4},
  {"vit_qkv",2160ull*3072*4},{"vit_attention",2160ull*1024*4},
  {"block70_prefix",129600ull*2048*4},{"block70_feature",3840ull*2160*32*4},
  {"block70_qkv",3840ull*2160*48*4},{"block70_body",3840ull*2160*32*4},
  {"block70_rgba",3840ull*2160*4*4},{"final_r10",3840ull*2160*4}
 };
 auto h=hp();std::vector<ID3D12Resource*>r;UINT64 total=0;for(auto&s:specs){auto desc=bd(s.bytes);ID3D12Resource*x=nullptr;HRESULT z=d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&x));if(FAILED(z)){std::fprintf(stderr,"allocation_failed name=%s bytes=%llu hr=0x%08lx\n",s.name,(unsigned long long)s.bytes,z);return 3;}r.push_back(x);total+=s.bytes;std::printf("allocated %-22s %8.2f MiB cumulative=%8.2f MiB\n",s.name,double(s.bytes)/MiB,double(total)/MiB);}
 DXGI_QUERY_VIDEO_MEMORY_INFO info{};IDXGIAdapter3*a3=nullptr;if(SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a3)))&&SUCCEEDED(a3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info)))std::printf("arena_total_mib=%.2f current_usage_mib=%.2f budget_mib=%.2f headroom_mib=%.2f\n",double(total)/MiB,double(info.CurrentUsage)/MiB,double(info.Budget)/MiB,double(info.Budget-info.CurrentUsage)/MiB);else std::printf("arena_total_mib=%.2f\n",double(total)/MiB);return 0;
}
