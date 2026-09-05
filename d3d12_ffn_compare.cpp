#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <vector>
#include <algorithm>
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion=721;
__declspec(dllexport) const char *D3D12SDKPath=".\\D3D12\\";
}
static void check(const char*n,HRESULT hr){if(FAILED(hr)){std::printf("%s=%08x\n",n,unsigned(hr));ExitProcess(1);}}
static std::vector<unsigned char> read(const wchar_t*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f){std::printf("missing=%ls\n",p);ExitProcess(2);}size_t n=size_t(f.tellg());std::vector<unsigned char>b(n);f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);if(!f)ExitProcess(2);return b;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=bytes;r.Height=1;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;r.Flags=flags;ID3D12Resource*x=nullptr;check("buffer",d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&x)));return x;}
static void transition(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=before;b.Transition.StateAfter=after;c->ResourceBarrier(1,&b);}
int wmain(int argc,wchar_t**argv){
    if(argc!=6){std::printf("usage: compare weights.bin packed.f16 input.f32 old.cso new.cso\n");return 2;}
    auto weights=read(argv[1]),packed=read(argv[2]),input=read(argv[3]);
    if(weights.size()!=41220||packed.size()!=8192||input.size()!=960ull*544*32*4)return 2;
    const IID experiment={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};check("experimental",D3D12EnableExperimentalFeatures(1,&experiment,nullptr,nullptr));
    IDXGIFactory6*f=nullptr;check("factory",CreateDXGIFactory1(IID_PPV_ARGS(&f)));IDXGIAdapter1*adapter=nullptr;for(UINT i=0;;i++){IDXGIAdapter1*x=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&x))==DXGI_ERROR_NOT_FOUND)break;if(x){DXGI_ADAPTER_DESC1 desc{};x->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=x;break;}x->Release();}}if(!adapter)return 3;
    ID3D12Device*d=nullptr;check("device",D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};check("queue",d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ID3D12CommandAllocator*alloc=nullptr;check("allocator",d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));ID3D12GraphicsCommandList*c=nullptr;check("list",d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_PPV_ARGS(&c)));
    ID3D12Fence*fence=nullptr;check("fence",d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 generation=0;
    auto submit=[&](){check("close",c->Close());ID3D12CommandList*lists[]={c};q->ExecuteCommandLists(1,lists);check("signal",q->Signal(fence,++generation));check("event",fence->SetEventOnCompletion(generation,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0){std::printf("timeout removed=%08x\n",unsigned(d->GetDeviceRemovedReason()));ExitProcess(4);}};
    std::vector<ID3D12Resource*>staging;
    auto upload=[&](const std::vector<unsigned char>&data){auto*src=buffer(d,data.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);auto*dst=buffer(d,data.size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);void*p=nullptr;D3D12_RANGE none{0,0};check("upload",src->Map(0,&none,&p));std::memcpy(p,data.data(),data.size());src->Unmap(0,nullptr);c->CopyResource(dst,src);transition(c,dst,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);staging.push_back(src);return dst;};
    auto*w=upload(weights),*mat=upload(packed),*in=upload(input);submit();for(auto*r:staging)r->Release();
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,4}};D3D12_ROOT_PARAMETER rp{};rp.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&rp;ID3DBlob*sig=nullptr,*error=nullptr;check("signature",D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&error));ID3D12RootSignature*root=nullptr;check("root",d->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&root)));
    ID3D12PipelineState*pso[2]{};for(UINT i=0;i<2;i++){ID3DBlob*code=nullptr;check("shader",D3DReadFileToBlob(argv[4+i],&code));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};check(i?"candidate pso":"baseline pso",d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));code->Release();}
    ID3D12Resource*out[2]={buffer(d,input.size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),buffer(d,input.size(),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)};
    auto*rb=buffer(d,input.size(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);auto*timeBuffer=buffer(d,16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_QUERY_HEAP_DESC queryDesc{};queryDesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;queryDesc.Count=2;ID3D12QueryHeap*queries=nullptr;check("queries",d->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&queries)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,14,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*heap=nullptr;check("heap",d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));UINT step=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT variant=0;variant<2;variant++){ID3D12Resource*resources[]={w,in,mat,w};UINT counts[]={10305,UINT(input.size()/4),2048,10305};for(UINT j=0;j<4;j++){D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Buffer.NumElements=counts[j];if(j==1)sv.Buffer.StructureByteStride=4;else{sv.Format=DXGI_FORMAT_R32_TYPELESS;sv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;}d->CreateShaderResourceView(resources[j],&sv,cpu);cpu.ptr+=step;}for(UINT j=0;j<3;j++){D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=UINT(input.size()/4);uv.Buffer.StructureByteStride=4;d->CreateUnorderedAccessView(out[variant],nullptr,&uv,cpu);cpu.ptr+=step;}}
    std::vector<float> reference(input.size()/4);UINT64 frequency=0;check("frequency",q->GetTimestampFrequency(&frequency));
    for(UINT variant=0;variant<2;variant++){
        check("reset allocator",alloc->Reset());check("reset list",c->Reset(alloc,pso[variant]));c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);auto gpu=heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(variant)*7*step;c->SetComputeRootDescriptorTable(0,gpu);
        for(UINT iteration=0;iteration<110;iteration++){if(iteration==10)c->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,0);c->Dispatch(8160,1,1);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=out[variant];c->ResourceBarrier(1,&b);}
        c->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,1);c->ResolveQueryData(queries,D3D12_QUERY_TYPE_TIMESTAMP,0,2,timeBuffer,0);transition(c,out[variant],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);c->CopyResource(rb,out[variant]);submit();
        void*p=nullptr;D3D12_RANGE tr{0,16},none{0,0};check("times",timeBuffer->Map(0,&tr,&p));auto*times=static_cast<UINT64*>(p);std::printf("variant=%u gpu_ms=%.6f iterations=100\n",variant,1000.0*double(times[1]-times[0])/frequency/100.0);timeBuffer->Unmap(0,&none);
        D3D12_RANGE rr{0,input.size()};check("output",rb->Map(0,&rr,&p));auto*values=static_cast<float*>(p);if(!variant)std::copy(values,values+reference.size(),reference.begin());else{double absolute=0,squared=0,maximum=0;size_t different=0,nonfinite=0;for(size_t i=0;i<reference.size();i++){double delta=double(values[i])-reference[i];nonfinite+=!std::isfinite(values[i])||!std::isfinite(reference[i]);absolute+=std::abs(delta);squared+=delta*delta;maximum=std::max(maximum,std::abs(delta));different+=values[i]!=reference[i];}std::printf("values=%zu different=%zu nonfinite=%zu mae=%.9f rmse=%.9f max=%.9f\n",reference.size(),different,nonfinite,absolute/reference.size(),std::sqrt(squared/reference.size()),maximum);}rb->Unmap(0,&none);
    }
    return 0;
}
