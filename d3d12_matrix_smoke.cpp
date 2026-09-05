#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdint>
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion=721;
__declspec(dllexport) const char *D3D12SDKPath=".\\D3D12\\";
}
void check(const char *name,HRESULT hr){if(FAILED(hr)){std::printf("%s=%08x\n",name,unsigned(hr));ExitProcess(1);}}
ID3D12Resource *buffer(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
    ID3D12Resource*r=nullptr;check("buffer",d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;
    const IID feature={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};
    check("experimental",D3D12EnableExperimentalFeatures(1,&feature,nullptr,nullptr));
    IDXGIFactory6*factory=nullptr;check("factory",CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    IDXGIAdapter1*adapter=nullptr;
    for(UINT i=0;;i++){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};if(c){c->GetDesc1(&desc);if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=c;break;}c->Release();}}
    if(!adapter)return 3;
    ID3D12Device*d=nullptr;check("device",D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ID3DBlob*shader=nullptr;check("shader",D3DReadFileToBlob(argv[1],&shader));
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;
    ID3DBlob*signature=nullptr,*error=nullptr;check("signature",D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));
    ID3D12RootSignature*root=nullptr;check("root",d->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};ID3D12PipelineState*pso=nullptr;check("pso",d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    auto*w=buffer(d,2048,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    constexpr UINT bytes=256*32*4;
    auto*out=buffer(d,bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto*readback=buffer(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    void*m=nullptr;D3D12_RANGE none{0,0};check("map weights",w->Map(0,&none,&m));for(UINT i=0;i<1024;i++)static_cast<uint16_t*>(m)[i]=0x3c00;w->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*heap=nullptr;check("heap",d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Buffer.NumElements=512;srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;d->CreateShaderResourceView(w,&srv,cpu);
    cpu.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=256*32;uv.Buffer.StructureByteStride=4;d->CreateUnorderedAccessView(out,nullptr,&uv,cpu);
    D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*q=nullptr;check("queue",d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ID3D12CommandAllocator*allocator=nullptr;check("allocator",d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ID3D12GraphicsCommandList*cmd=nullptr;check("list",d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pso,IID_PPV_ARGS(&cmd)));
    cmd->SetDescriptorHeaps(1,&heap);cmd->SetComputeRootSignature(root);cmd->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());cmd->Dispatch(8,1,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=out;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;cmd->ResourceBarrier(1,&barrier);cmd->CopyResource(readback,out);check("close",cmd->Close());
    ID3D12CommandList*lists[]={cmd};q->ExecuteCommandLists(1,lists);ID3D12Fence*fence=nullptr;check("fence",d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));check("signal",q->Signal(fence,1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check("event",fence->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0){std::printf("gpu_timeout removed=%08x\n",unsigned(d->GetDeviceRemovedReason()));return 4;}
    D3D12_RANGE range{0,bytes};check("readback",readback->Map(0,&range,&m));UINT wrong=0;for(UINT t=0;t<256;t++)for(UINT c=0;c<32;c++)wrong+=static_cast<float*>(m)[t*32+c]!=16.5f*(t%8+1);
    std::printf("matrix_smoke outputs=8192 mismatches=%u first=%f last=%f\n",wrong,static_cast<float*>(m)[0],static_cast<float*>(m)[8191]);readback->Unmap(0,&none);
    return wrong?5:0;
}
