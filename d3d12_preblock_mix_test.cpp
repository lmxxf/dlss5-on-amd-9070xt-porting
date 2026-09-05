#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static void ck(const char *name, HRESULT result) {
    if (FAILED(result)) { std::fprintf(stderr, "%s=%08lx\n", name, result); ExitProcess(1); }
}
static std::vector<unsigned char> rd(const wchar_t *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate); if (!f) ExitProcess(2);
    size_t n = static_cast<size_t>(f.tellg()); f.seekg(0);
    std::vector<unsigned char> b(n); f.read(reinterpret_cast<char *>(b.data()), n); return b;
}
static D3D12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{}; h.Type=type; h.CreationNodeMask=h.VisibleNodeMask=1; return h;
}
static D3D12_RESOURCE_DESC bd(UINT64 size, D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=size;
    d.Height=1; d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=flags; return d;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 6 && argc != 7) {
        std::fwprintf(stderr,L"usage: %ls mix-weights.f32 input.rgba32f oracle.f32 output.f32 shader.hlsl\n",argv[0]);
        return 2;
    }
    const bool raw_output=argc==7&&(!wcscmp(argv[6],L"ffn-raw")||!wcscmp(argv[6],L"attention-raw")); const bool attention_mode=argc==7&&(!wcscmp(argv[6],L"attention")||!wcscmp(argv[6],L"attention-raw")); const bool ffn_mode=argc==7&&(!wcscmp(argv[6],L"ffn")||!wcscmp(argv[6],L"ffn-raw")); auto matrix=rd(argv[1]),input=rd(argv[2]),oracle=rd(argv[3]);
    const UINT inputs=attention_mode?32:4,outputs=32;
    const UINT spatial_width=argc==9?_wtoi(argv[7]):1,phase_period=argc==9?_wtoi(argv[8]):1;
    const UINT phases=phase_period*phase_period;
    const UINT64 samples=inputs?input.size()/(UINT64(inputs)*4):0;
    const UINT64 total_outputs=samples*outputs;
    if(attention_mode && samples%64!=0)return 2; // Whole 8x8 windows only.
    if(!inputs||!outputs||!samples||!spatial_width||!phase_period||matrix.size()!=UINT64(attention_mode?8225:ffn_mode?8736:512)*4||input.size()!=samples*inputs*4||oracle.size()!=total_outputs*4)return 2;
    IDXGIFactory6*factory=nullptr;ck("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    IDXGIAdapter1*adapter=nullptr;DXGI_ADAPTER_DESC1 ad{};
    for(UINT i=0;;++i){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};c->GetDesc1(&d);if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(d.Description,L"AMD")){adapter=c;ad=d;break;}c->Release();}
    if(!adapter)return 1;ID3D12Device*dev=nullptr;ck("device",D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dev)));std::wprintf(L"adapter: %ls\n",ad.Description);
    D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*q=nullptr;ck("queue",dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*ca=nullptr;ck("allocator",dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&ca)));ID3D12GraphicsCommandList*cl=nullptr;ck("list",dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,ca,nullptr,IID_PPV_ARGS(&cl)));
    const auto shader=rd(argv[5]);
    char input_count[16],output_count[16],total_count[32],spatial_count[16],period_count[16];std::snprintf(input_count,sizeof(input_count),"%u",inputs);std::snprintf(output_count,sizeof(output_count),"%u",outputs);std::snprintf(total_count,sizeof(total_count),"%llu",static_cast<unsigned long long>(total_outputs));std::snprintf(spatial_count,sizeof(spatial_count),"%u",spatial_width);std::snprintf(period_count,sizeof(period_count),"%u",phase_period);const bool live_profile=std::getenv("DLSS5_PREBLOCK_LIVE_PROFILE")!=nullptr;const char*seed=std::getenv("DLSS5_PREBLOCK_SEED");D3D_SHADER_MACRO macros[]={{"INPUTS",input_count},{"OUTPUTS",output_count},{"TOTAL_OUTPUTS",total_count},{"SPATIAL_WIDTH",spatial_count},{"PHASE_PERIOD",period_count},{"DEBUG_FEATURES",argc==7&&!ffn_mode?"1":"0"},{"FULL_FFN",ffn_mode?"1":"0"},{"RAW_OUTPUT",raw_output?"1":"0"},{"LIVE_PROFILE",live_profile?"1":"0"},{"NOISE_SEED",seed?seed:(live_profile?"0":"1065353216")},{nullptr,nullptr}};ID3DBlob*cs=nullptr,*err=nullptr;const HRESULT compile_hr=D3DCompile(shader.data(),shader.size(),nullptr,macros,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&cs,&err);if(FAILED(compile_hr)&&err)std::fprintf(stderr,"%.*s\n",int(err->GetBufferSize()),static_cast<const char*>(err->GetBufferPointer()));ck("compile",compile_hr);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2};D3D12_ROOT_PARAMETER rp{};rp.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rsd{};rsd.NumParameters=1;rsd.pParameters=&rp;ID3DBlob*rsb=nullptr;ck("serialize",D3D12SerializeRootSignature(&rsd,D3D_ROOT_SIGNATURE_VERSION_1,&rsb,&err));ID3D12RootSignature*rs=nullptr;ck("rootsig",dev->CreateRootSignature(0,rsb->GetBufferPointer(),rsb->GetBufferSize(),IID_PPV_ARGS(&rs)));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=rs;pd.CS={cs->GetBufferPointer(),cs->GetBufferSize()};ID3D12PipelineState*pso=nullptr;ck("pso",dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    const UINT64 output_bytes=total_outputs*4;auto upload=hp(D3D12_HEAP_TYPE_UPLOAD),def=hp(D3D12_HEAP_TYPE_DEFAULT),read=hp(D3D12_HEAP_TYPE_READBACK);ID3D12Resource*in[2]{};std::vector<unsigned char>*bytes[2]={&matrix,&input};for(int i=0;i<2;i++){auto d=bd(bytes[i]->size());ck("input resource",dev->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&in[i])));void*m;D3D12_RANGE z{0,0};in[i]->Map(0,&z,&m);memcpy(m,bytes[i]->data(),bytes[i]->size());in[i]->Unmap(0,nullptr);}auto od=bd(output_bytes,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),rbd=bd(output_bytes);ID3D12Resource*out=nullptr,*rb=nullptr;ck("output",dev->CreateCommittedResource(&def,D3D12_HEAP_FLAG_NONE,&od,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));ck("readback",dev->CreateCommittedResource(&read,D3D12_HEAP_FLAG_NONE,&rbd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    D3D12_DESCRIPTOR_HEAP_DESC dhd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*dh=nullptr;ck("heap",dev->CreateDescriptorHeap(&dhd,IID_PPV_ARGS(&dh)));UINT stride=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto h=dh->GetCPUDescriptorHandleForHeapStart();for(int i=0;i<2;i++){D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Buffer.StructureByteStride=4;sv.Buffer.NumElements=bytes[i]->size()/4;dev->CreateShaderResourceView(in[i],&sv,h);h.ptr+=stride;}D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.StructureByteStride=4;uv.Buffer.NumElements=static_cast<UINT>(total_outputs);dev->CreateUnorderedAccessView(out,nullptr,&uv,h);
    ID3D12DescriptorHeap*heaps[]={dh};cl->SetDescriptorHeaps(1,heaps);cl->SetComputeRootSignature(rs);cl->SetComputeRootDescriptorTable(0,dh->GetGPUDescriptorHandleForHeapStart());cl->SetPipelineState(pso);cl->Dispatch(static_cast<UINT>((total_outputs+63)/64),1,1);D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=out;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;cl->ResourceBarrier(1,&barrier);cl->CopyBufferRegion(rb,0,out,0,output_bytes);ck("close",cl->Close());
    LARGE_INTEGER freq,t0,t1;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&t0);ID3D12CommandList*lists[]={cl};q->ExecuteCommandLists(1,lists);ID3D12Fence*fence=nullptr;ck("fence",dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));q->Signal(fence,1);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);fence->SetEventOnCompletion(1,event);WaitForSingleObject(event,INFINITE);QueryPerformanceCounter(&t1);
    void*m=nullptr;D3D12_RANGE all{0,static_cast<SIZE_T>(output_bytes)};rb->Map(0,&all,&m);const float*got=static_cast<const float*>(m),*want=reinterpret_cast<const float*>(oracle.data());double ae=0,se=0,sg=0,sw=0,sgw=0;float mx=0;for(size_t i=0;i<total_outputs;i++){double d=got[i]-want[i];ae+=std::abs(d);se+=d*d;mx=std::max(mx,static_cast<float>(std::abs(d)));sg+=got[i]*got[i];sw+=want[i]*want[i];sgw+=got[i]*want[i];}std::ofstream(argv[4],std::ios::binary).write(reinterpret_cast<const char*>(got),output_bytes);std::printf("submit_to_fence_ms=%.3f samples=%llu outputs=%u MAE=%.9g RMSE=%.9g max=%.9g cosine=%.12g\n",1000.0*(t1.QuadPart-t0.QuadPart)/freq.QuadPart,static_cast<unsigned long long>(samples),outputs,ae/total_outputs,std::sqrt(se/total_outputs),mx,sgw/std::sqrt(sg*sw));return 0;
}
