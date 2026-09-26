#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "/tmp/re9-upstream-bridge-review/OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/backend/lmxxf_runtime/LmxxfNrApi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstddef>
#include <chrono>
#include <vector>
#include <algorithm>

static void Require(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

static std::wstring Widen(const char *s) { return std::wstring(s, s + std::strlen(s)); }

static void Check(HRESULT hr, const char *what)
{
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FAIL: %s hr=%08lx\n", what, static_cast<unsigned long>(hr));
        std::exit(1);
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "usage: nr_bench.exe <LmxxfNrRuntime.dll> <modules_dir> <width> <height> <frames>\n");
        return 2;
    }

    setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE dll = LoadLibraryW(argv[1]);
    Require(dll != nullptr, "LoadLibraryW");
    auto getApi = reinterpret_cast<int32_t (*)(uint32_t, LmxxfNrApi *)>(GetProcAddress(dll, "LmxxfNrGetApi"));
    Require(getApi != nullptr, "GetProcAddress");

    static unsigned char apibuf[4096];LmxxfNrApi& api=*reinterpret_cast<LmxxfNrApi*>(apibuf);bool got=false;
    uint32_t abi_used=0;for(uint32_t abi=1;abi<=4&&!got;abi++)for(uint32_t sz=sizeof(LmxxfNrApi);sz<=sizeof(LmxxfNrApi)+512&&!got;sz+=8){
      std::memset(apibuf,0,sizeof apibuf);api.struct_size=sz;if(getApi(abi,&api)==LMXXF_NR_OK){got=true;abi_used=abi;std::printf("GetApi ok abi=%u struct_size=%u (ours %zu)\n",abi,sz,sizeof(LmxxfNrApi));}}
    Require(got,"GetApi");
    LmxxfNrCapabilities caps {};
    caps.struct_size = sizeof(caps);
    Require(api.QueryCapabilities(&caps) == LMXXF_NR_OK, "QueryCapabilities");
    

    IDXGIFactory4 *factory = nullptr;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    IDXGIAdapter1 *adapter = nullptr;
    ID3D12Device *device = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc {};
        adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x1002 && SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
            break;
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();
    Require(device != nullptr, "AMD D3D12 device");

    D3D12_COMMAND_QUEUE_DESC qd {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr;
    Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "queue");
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12CommandAllocator *outAlloc = nullptr;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "allocator");
    ID3D12GraphicsCommandList *list = nullptr;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list)), "list");

    D3D12_HEAP_PROPERTIES hp {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    const UINT W=(UINT)std::wcstol(argv[3],nullptr,10),H=(UINT)std::wcstol(argv[4],nullptr,10);const int N=(int)std::wcstol(argv[5],nullptr,10);
    td.Width = W;
    td.Height = H;
    td.DepthOrArraySize = td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource *color = nullptr;
    Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                          nullptr, IID_PPV_ARGS(&color)),
          "color");

    const bool scaled=false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;
    device->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource* upload=nullptr;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    void* mapped=nullptr;D3D12_RANGE empty{0,0};Check(upload->Map(0,&empty,&mapped),"map");
    for(UINT y=0;y<td.Height;y++)for(UINT x=0;x<td.Width;x++){uint16_t* p=(uint16_t*)((char*)mapped+y*fp.Footprint.RowPitch+8*x);p[0]=0x3800;p[1]=0x3400;p[2]=0x3000;p[3]=0x3C00;}
    upload->Unmap(0,nullptr);
    D3D12_RESOURCE_BARRIER bar{};bar.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;bar.Transition={color,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
    list->ResourceBarrier(1,&bar);
    D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=color;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    bar.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;bar.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&bar);

    ID3D12Resource *exposure=nullptr,*exposureUpload=nullptr;
    auto ed=td;ed.Width=ed.Height=1;ed.Format=DXGI_FORMAT_R32_FLOAT;
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&ed,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&exposure)),"exposure texture");
    auto ebd=bd;ebd.Width=256;hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&ebd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&exposureUpload)),"exposure upload");
    Check(exposureUpload->Map(0,&empty,&mapped),"exposure map");*(float*)mapped=scaled?1.f/32.f:1.f;exposureUpload->Unmap(0,nullptr);
    dst.pResource=exposure;src.pResource=exposureUpload;src.PlacedFootprint={};src.PlacedFootprint.Footprint={DXGI_FORMAT_R32_FLOAT,1,1,1,256};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    bar.Transition={exposure,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};list->ResourceBarrier(1,&bar);

    const std::wstring modules = argv[2];
    LmxxfNrCreateInfo info {};
    info.struct_size = sizeof(info);
    info.device = device;
    info.queue = queue;
    info.assets_directory = modules.c_str();
    void *ctx = nullptr;
    std::printf("step %s\n","create");{const auto crc=api.Create(&info,&ctx);if(crc!=LMXXF_NR_OK){char e[512]{};if(api.GetLastError)api.GetLastError(e,sizeof e);std::fprintf(stderr,"Create rc=%d %s\n",crc,e);return 1;}}
    std::printf("step %s\n","session");const int32_t prep = api.PrepareSession(ctx);
    if (prep != LMXXF_NR_OK)
    {
        char err[256] {};
        api.GetLastError(err, sizeof err);
        std::fprintf(stderr, "PrepareSession rc=%d err=%s\n", prep, err);
        Require(false, "PrepareSession");
    }

    LmxxfNrFrameInfo frame {};
    frame.struct_size = sizeof(frame);
    frame.color_width = W;
    frame.color_height = H;
    frame.color = color;
    frame.exposure=exposure;frame.exposure_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;frame.pre_exposure=frame.exposure_scale=1.f;
    static unsigned char framebuf[1024];std::memset(framebuf,0,sizeof framebuf);
    if(abi_used==1){
      const uint32_t v1=(uint32_t)offsetof(LmxxfNrFrameInfo,exposure);std::memcpy(framebuf,&frame,v1);
      // probe their frame struct_size with a throwaway context-free call pattern: try sizes until PrepareFrame stops saying struct_size mismatch
      *reinterpret_cast<void**>(framebuf+v1)=exposure;std::printf("v1 prefix=%u (+exposure ptr)\n",v1);
    } else std::memcpy(framebuf,&frame,sizeof frame);
    frame.color_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    // first submit the upload copy recorded in `list`
    Check(list->Close(),"close setup");{ID3D12CommandList* l[]={list};queue->ExecuteCommandLists(1,l);}
    ID3D12Fence* fence=nullptr;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 fv=0;
    auto waitq=[&](){Check(queue->Signal(fence,++fv),"signal");Check(fence->SetEventOnCompletion(fv,ev),"completion");Require(WaitForSingleObject(ev,30000)==WAIT_OBJECT_0,"timeout");};
    waitq();
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&outAlloc)), "out alloc");
    ID3D12GraphicsCommandList* consumer=nullptr;
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,outAlloc,nullptr,IID_PPV_ARGS(&consumer)),"consumer");Check(consumer->Close(),"c0");
std::printf("step %s\n","probe");    if(abi_used==1){bool ok=false;
      for(uint32_t sz=(uint32_t)offsetof(LmxxfNrFrameInfo,exposure);sz<=512&&!ok;sz+=4){
        *reinterpret_cast<uint32_t*>(framebuf)=sz;Check(alloc->Reset(),"par");Check(list->Reset(alloc,nullptr),"plr");
        LmxxfNrJob job{};job.struct_size=sizeof(job);std::printf("try sz=%u\n",sz);const auto r=api.PrepareFrame(ctx,reinterpret_cast<LmxxfNrFrameInfo*>(framebuf),&job);std::printf(" rc=%d\n",r);
        char e[512]{};api.GetLastError(e,sizeof e);
        if(!std::strstr(e,"struct_size")){std::printf("frame struct_size=%u rc=%d %s\n",sz,r,e);ok=true;
          if(r==LMXXF_NR_OK){std::printf("RI\n");api.RecordInputs(ctx,job.handle,list);std::printf("RO\n");Check(list->Close(),"pcl");Check(outAlloc->Reset(),"por");Check(consumer->Reset(outAlloc,nullptr),"pcr");api.RecordOutputs(ctx,job.handle,consumer);Check(consumer->Close(),"pcc");{ID3D12CommandList* l[]={list};queue->ExecuteCommandLists(1,l);}std::printf("EH\n");api.EnqueueHip(ctx,job.handle);std::printf("EH done\n");{ID3D12CommandList* l[]={consumer};queue->ExecuteCommandLists(1,l);}api.Retire(ctx,job.handle);waitq();}
          else return 1;}
        else Check(list->Close(),"pclose");
      }
      Require(ok,"frame struct_size probe");}
    std::vector<double> ms;
    for(int f=0;f<N;f++){
      auto t0=std::chrono::steady_clock::now();
      Check(alloc->Reset(),"ar");Check(list->Reset(alloc,nullptr),"lr");Check(outAlloc->Reset(),"or");Check(consumer->Reset(outAlloc,nullptr),"cr");
      LmxxfNrJob job{};job.struct_size=sizeof(job);
      const auto frc=api.PrepareFrame(ctx,reinterpret_cast<LmxxfNrFrameInfo*>(framebuf),&job);if(frc!=LMXXF_NR_OK){char e[512]{};api.GetLastError(e,sizeof e);std::fprintf(stderr,"PrepareFrame rc=%d %s\n",frc,e);return 1;}
      Require(api.RecordInputs(ctx,job.handle,list)==LMXXF_NR_OK,"RecordInputs");Check(list->Close(),"cl");
      Require(api.RecordOutputs(ctx,job.handle,consumer)==LMXXF_NR_OK,"RecordOutputs");Check(consumer->Close(),"cc");
      {ID3D12CommandList* l[]={list};queue->ExecuteCommandLists(1,l);}
      const auto hip=api.EnqueueHip(ctx,job.handle);if(hip!=LMXXF_NR_OK){char e[512]{};api.GetLastError(e,sizeof e);std::fprintf(stderr,"HIP rc=%d %s\n",hip,e);return 1;}
      {ID3D12CommandList* l[]={consumer};queue->ExecuteCommandLists(1,l);}
      Require(api.Retire(ctx,job.handle)==LMXXF_NR_OK,"Retire");
      waitq();
      ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
    }
    Check(device->GetDeviceRemovedReason(),"device healthy");
    std::vector<double> s(ms.begin()+std::min<size_t>(ms.size(),N/5),ms.end());std::sort(s.begin(),s.end());
    double sum=0;for(double v:s)sum+=v;
    std::printf("nr_bench %ux%u frames=%d used=%zu mean_ms=%.3f median_ms=%.3f p10=%.3f p90=%.3f\n",W,H,N,s.size(),sum/s.size(),s[s.size()/2],s[s.size()/10],s[s.size()*9/10]);
    CloseHandle(ev);fence->Release();consumer->Release();
    Require(api.Destroy(ctx) == LMXXF_NR_OK, "Destroy");
    exposure->Release();exposureUpload->Release();
    upload->Release();
    color->Release();
    list->Release();
    if (outAlloc)
        outAlloc->Release();
    alloc->Release();
    queue->Release();
    device->Release();
    if (adapter)
        adapter->Release();
    FreeLibrary(dll);
    std::printf("lmxxf_nr_gpu: ok\n");
    return 0;
}
