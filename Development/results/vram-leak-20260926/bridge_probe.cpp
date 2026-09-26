// Real bridge lifecycle probe: create D3D12Bridge per session at cycling tiers, run one staged network frame, destroy; log memory.
#include "hip_d3d12_bridge.h"
#include <psapi.h>
static void C(HRESULT h,const char*w){if(FAILED(h)){std::fprintf(stderr,"FAIL %s %08lx\n",w,(unsigned long)h);std::exit(1);}}
int main(int argc,char**argv){
 if(argc<3){std::fprintf(stderr,"usage: bridge_probe <assets_dir> <sessions>\n");return 2;}
 std::string assets=argv[1];int N=atoi(argv[2]);
 IDXGIFactory4*f{};C(CreateDXGIFactory1(IID_PPV_ARGS(&f)),"factory");IDXGIAdapter1*ad{};ID3D12Device*dev{};
 for(UINT i=0;f->EnumAdapters1(i,&ad)!=DXGI_ERROR_NOT_FOUND;i++){DXGI_ADAPTER_DESC1 d{};ad->GetDesc1(&d);if(d.VendorId==0x1002&&SUCCEEDED(D3D12CreateDevice(ad,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dev))))break;ad->Release();ad=nullptr;}
 IDXGIAdapter3*a3{};ad->QueryInterface(IID_PPV_ARGS(&a3));
 D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ID3D12CommandQueue*q{};C(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)),"queue");
 auto report=[&](int it,unsigned w,unsigned h,const char*tag){DXGI_QUERY_VIDEO_MEMORY_INFO vi{};a3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&vi);PROCESS_MEMORY_COUNTERS_EX pm{};pm.cb=sizeof pm;GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&pm,sizeof pm);
  std::printf("it=%d %s %ux%u dxgi_local_MiB=%.1f private_MiB=%.1f\n",it,tag,w,h,vi.CurrentUsage/1048576.,pm.PrivateUsage/1048576.);std::fflush(stdout);};
 const unsigned tiers[][2]={{1920,1152},{1600,960},{1280,768},{1920,1152}};
 report(-1,0,0,"start");
 for(int it=0;it<N;it++){auto&t=tiers[it%4];
  {hip_reference::D3D12Bridge b;hip_reference::Options o;o.width=t[0];o.height=t[1];o.post_shift=3;o.fast_vit=true;o.wmma=o.wave=o.tiled=o.pooled=true;o.assets=assets;
   o.fast_c32=o.fused_c32=o.fused_ffn=o.fast_mh=o.fused_mh=o.mh_wave=o.fast_deep=o.fast_prefix=o.packed_weights=o.packed_c32=o.fp8_normalized=o.fp8_ffn=o.fp8_av=o.fp8_deep=o.fp8_middle=o.half_c32=o.crop_c32=o.fused_qkv_norm=o.fused_mh_ffn=o.tiled_mh_ffn=o.mapped_c32=o.vit_blocked=o.vit_contract_blocked=true;o.tiled_ffn_min_c=256;
   o.skip_blocks=hip_reference::ParseSkipBlocks("42,43,46");o.modules=assets+"\\HIP";
   b.Create(q,o,std::vector<float>());b.PrepareStagedKernels();
   // read back the output buffer and hash it (FNV-1a 64)
   auto*out=b.Output();auto od=out->GetDesc();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd=od;rd.Flags=D3D12_RESOURCE_FLAG_NONE;ID3D12Resource*rb{};C(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)),"readback");
   ID3D12CommandAllocator*al{};ID3D12GraphicsCommandList*cl{};C(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&al)),"al");C(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,al,nullptr,IID_PPV_ARGS(&cl)),"cl");
   cl->CopyBufferRegion(rb,0,out,0,od.Width);C(cl->Close(),"close");ID3D12CommandList*ls[]={cl};q->ExecuteCommandLists(1,ls);ID3D12Fence*fe{};C(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fe)),"fence");q->Signal(fe,1);HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);fe->SetEventOnCompletion(1,ev);WaitForSingleObject(ev,30000);
   void*m{};D3D12_RANGE r{0,(SIZE_T)od.Width};C(rb->Map(0,&r,&m),"map");unsigned long long hsh=1469598103934665603ull;for(size_t i=0;i<od.Width;i++){hsh^=((unsigned char*)m)[i];hsh*=1099511628211ull;}D3D12_RANGE z{0,0};rb->Unmap(0,&z);
   std::printf("it=%d hash %ux%u %016llx\n",it,t[0],t[1],hsh);CloseHandle(ev);fe->Release();cl->Release();al->Release();rb->Release();
   report(it,t[0],t[1],"live");}
  report(it,t[0],t[1],"after-destroy");}
 return 0;}
