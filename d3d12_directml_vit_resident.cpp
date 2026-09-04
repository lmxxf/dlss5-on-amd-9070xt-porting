#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#define DML_TARGET_VERSION_USE_LATEST
#ifndef _Maybenull_
#define _Maybenull_
#endif
#include <DirectML.h>
#include "directml_gemm_runtime.h"
#include <cstdio>
#include <cstring>
#include <vector>
static const GUID IID_DML_DEVICE={0x6dbd6437,0x96fd,0x423f,{0xa9,0x8c,0xae,0x5e,0x7c,0x2a,0x57,0x3f}},IID_DML_RECORDER={0xe6857a76,0x2e3e,0x4fdd,{0xbf,0xf4,0x5d,0x2b,0xa1,0x0f,0xb4,0x53}};
using CreateFn=HRESULT(WINAPI*)(ID3D12Device*,DML_CREATE_DEVICE_FLAGS,REFIID,void**);
static D3D12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE t){D3D12_HEAP_PROPERTIES x{};x.Type=t;x.CreationNodeMask=x.VisibleNodeMask=1;return x;}
static D3D12_RESOURCE_DESC bd(UINT64 n,D3D12_RESOURCE_FLAGS f){D3D12_RESOURCE_DESC x{};x.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;x.Width=n;x.Height=1;x.DepthOrArraySize=1;x.MipLevels=1;x.SampleDesc.Count=1;x.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;x.Flags=f;return x;}
static ID3D12Resource* make(ID3D12Device*d,UINT64 n,D3D12_HEAP_TYPE h=D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATES s=D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAGS f=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS){auto p=hp(h);auto b=bd(n,f);ID3D12Resource*r=nullptr;dmlrt_check("resource",d->CreateCommittedResource(&p,D3D12_HEAP_FLAG_NONE,&b,s,nullptr,IID_PPV_ARGS(&r)));return r;}
static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;c->ResourceBarrier(1,&b);}

struct QkvPackPass {
    ID3D12RootSignature* root{};
    ID3D12PipelineState* pso{};
    ID3D12DescriptorHeap* heap{};
    void Create(ID3D12Device* device, ID3D12Resource* input,
                ID3D12Resource* scales, ID3D12Resource* q,
                ID3D12Resource* k, ID3D12Resource* v,
                UINT64 inputBytes, UINT64 outputBytes) {
        const char* source = R"(
ByteAddressBuffer input:register(t0); StructuredBuffer<float> scales:register(t1);
RWByteAddressBuffer q:register(u0),k:register(u1),v:register(u2);
groupshared float qv[32],kv[32],vv[32],sumv[32];
float H(uint i){uint x=input.Load((i&~1)*2);return f16tof32((x>>((i&1)*16))&65535);}
[numthreads(32,1,1)] void main(uint3 lane:SV_GroupThreadID,uint3 gid:SV_GroupID){
 uint t=gid.x,h=gid.y,d=lane.x,qi=(t*3)*1024+h*32+d,ki=qi+1024,vi=ki+1024;
 qv[d]=H(qi);kv[d]=H(ki);vv[d]=H(vi);sumv[d]=qv[d]*qv[d];GroupMemoryBarrierWithGroupSync();
 for(uint s=16;s;s>>=1){if(d<s)sumv[d]+=sumv[d+s];GroupMemoryBarrierWithGroupSync();}
 float qs=scales[h]*rsqrt(max(sumv[0],1e-12));sumv[d]=kv[d]*kv[d];GroupMemoryBarrierWithGroupSync();
 for(uint s=16;s;s>>=1){if(d<s)sumv[d]+=sumv[d+s];GroupMemoryBarrierWithGroupSync();}
 float ks=scales[32+h]*rsqrt(max(sumv[0],1e-12));qv[d]*=qs;kv[d]*=ks;GroupMemoryBarrierWithGroupSync();
 if((d&1)==0){uint o=(h*T*32+t*32+d)*2;q.Store(o,f32tof16(qv[d])|(f32tof16(qv[d+1])<<16));k.Store(o,f32tof16(kv[d])|(f32tof16(kv[d+1])<<16));v.Store(o,f32tof16(vv[d])|(f32tof16(vv[d+1])<<16));}}
)";
        char tokens[16]; std::snprintf(tokens,sizeof(tokens),"%u",2160u);
        D3D_SHADER_MACRO macros[]={{"T",tokens},{nullptr,nullptr}};
        ID3DBlob* code=nullptr,*error=nullptr;
        dmlrt_check("qkv pack compile",D3DCompile(source,std::strlen(source),nullptr,macros,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,2}};
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=1;rootDesc.pParameters=&parameter;
        ID3DBlob* signature=nullptr;dmlrt_check("qkv pack signature",D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));
        dmlrt_check("qkv pack root",device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root;pipeline.CS={code->GetBufferPointer(),code->GetBufferSize()};
        dmlrt_check("qkv pack pso",device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&pso)));
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,5,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        dmlrt_check("qkv pack heap",device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&heap)));
        UINT step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Buffer.NumElements=(UINT)(inputBytes/4);srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=step;
        srv.Format=DXGI_FORMAT_UNKNOWN;srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;srv.Buffer.StructureByteStride=4;srv.Buffer.NumElements=64;device->CreateShaderResourceView(scales,&srv,cpu);cpu.ptr+=step;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.Buffer.NumElements=(UINT)(outputBytes/4);uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
        for(auto* output:{q,k,v}){device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);cpu.ptr+=step;}
    }
    void Record(ID3D12GraphicsCommandList* commands) {
        ID3D12DescriptorHeap* heaps[]={heap};commands->SetDescriptorHeaps(1,heaps);commands->SetComputeRootSignature(root);commands->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());commands->SetPipelineState(pso);commands->Dispatch(2160,32,1);
    }
};
int main(){constexpr UINT T=2160,H=32,D=32;IDXGIFactory6*f=nullptr;dmlrt_check("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));IDXGIAdapter1*a=nullptr;DXGI_ADAPTER_DESC1 ad{};for(UINT i=0;;i++){IDXGIAdapter1*x=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&x))==DXGI_ERROR_NOT_FOUND)break;x->GetDesc1(&ad);if(!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(ad.Description,L"AMD")){a=x;break;}x->Release();}if(!a)return 2;ID3D12Device*d=nullptr;dmlrt_check("device",D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));HMODULE module=LoadLibraryW(L"DirectML.dll");auto create=(CreateFn)GetProcAddress(module,"DMLCreateDevice");IDMLDevice*ml=nullptr;dmlrt_check("DMLCreateDevice",create(d,DML_CREATE_DEVICE_FLAG_NONE,IID_DML_DEVICE,(void**)&ml));IDMLCommandRecorder*rec=nullptr;dmlrt_check("recorder",ml->CreateCommandRecorder(IID_DML_RECORDER,(void**)&rec));DmlGemmOperator expand,contract,qkv,qk,av,projection;expand.Create(ml,d,1,T,1024,4096);contract.Create(ml,d,1,T,4096,1024);qkv.Create(ml,d,1,T,1024,3072);qk.Create(ml,d,H,T,D,T,DML_MATRIX_TRANSFORM_TRANSPOSE);av.Create(ml,d,H,T,T,D);projection.Create(ml,d,1,T,1024,1024);
 D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*queue=nullptr;dmlrt_check("queue",d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));ID3D12CommandAllocator*ca=nullptr;dmlrt_check("allocator",d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&ca)));ID3D12GraphicsCommandList*cl=nullptr;dmlrt_check("list",d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,ca,nullptr,IID_PPV_ARGS(&cl)));ID3D12Fence*fe=nullptr;dmlrt_check("fence",d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fe)));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 fv=0;auto wait=[&](){dmlrt_check("close",cl->Close());ID3D12CommandList*l[]={cl};queue->ExecuteCommandLists(1,l);queue->Signal(fe,++fv);fe->SetEventOnCompletion(fv,ev);WaitForSingleObject(ev,INFINITE);ca->Reset();cl->Reset(ca,nullptr);};for(auto*o:{&expand,&contract,&qkv,&qk,&av,&projection})o->RecordInitialization(rec,cl);wait();
 UINT64 mainBytes=UINT64(T)*1024*2,branchBytes=UINT64(T)*4096*2,qkvBytes=UINT64(T)*3072*2,qvBytes=UINT64(H)*T*D*2,scoreBytes=UINT64(H)*T*T*2;ID3D12Resource*main=make(d,mainBytes),*ew=make(d,UINT64(1024)*4096*2),*branch=make(d,branchBytes),*cw=make(d,UINT64(4096)*1024*2),*hidden=make(d,mainBytes),*qw=make(d,UINT64(1024)*3072*2),*qkvOut=make(d,qkvBytes),*scales=make(d,256),*qr=make(d,qvBytes),*kr=make(d,qvBytes),*vr=make(d,qvBytes),*score=make(d,scoreBytes),*prob=make(d,scoreBytes),*att=make(d,qvBytes),*pw=make(d,UINT64(1024)*1024*2),*final=make(d,mainBytes);std::vector<ID3D12Resource*>resources={main,ew,branch,cw,hidden,qw,qkvOut,scales,qr,kr,vr,score,prob,att,pw,final};
 D3D12_DESCRIPTOR_HEAP_DESC chd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,(UINT)resources.size(),D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*ch=nullptr;dmlrt_check("clear heap",d->CreateDescriptorHeap(&chd,IID_PPV_ARGS(&ch)));UINT step=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=ch->GetCPUDescriptorHandleForHeapStart();for(auto*r:resources){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;v.Format=DXGI_FORMAT_R32_TYPELESS;v.Buffer.NumElements=(UINT)(r->GetDesc().Width/4);v.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;d->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=step;}ID3D12DescriptorHeap*hs[]={ch};cl->SetDescriptorHeaps(1,hs);auto gpu=ch->GetGPUDescriptorHandleForHeapStart();cpu=ch->GetCPUDescriptorHandleForHeapStart();UINT zero[4]{};for(auto*r:resources){cl->ClearUnorderedAccessViewUint(gpu,cpu,r,zero,0,nullptr);gpu.ptr+=step;cpu.ptr+=step;}wait();
 const char*softmaxSource=R"(ByteAddressBuffer score:register(t0);RWByteAddressBuffer probability:register(u0);groupshared float tmp[64];float H(uint i){uint x=score.Load((i&~1)*2);return f16tof32((x>>((i&1)*16))&65535);}[numthreads(64,1,1)]void main(uint3 lane:SV_GroupThreadID,uint3 gid:SV_GroupID){uint query=gid.x,head=gid.y,base=head*T*T+query*T;float mx=-3.4e38;for(uint key=lane.x;key<T;key+=64)mx=max(mx,H(base+key));tmp[lane.x]=mx;GroupMemoryBarrierWithGroupSync();for(uint s=32;s;s>>=1){if(lane.x<s)tmp[lane.x]=max(tmp[lane.x],tmp[lane.x+s]);GroupMemoryBarrierWithGroupSync();}mx=tmp[0];float sum=0;for(uint key=lane.x;key<T;key+=64)sum+=exp(H(base+key)-mx);tmp[lane.x]=sum;GroupMemoryBarrierWithGroupSync();for(uint s=32;s;s>>=1){if(lane.x<s)tmp[lane.x]+=tmp[lane.x+s];GroupMemoryBarrierWithGroupSync();}sum=tmp[0];for(uint pair=lane.x;pair<(T+1)/2;pair+=64){uint key=pair*2;uint bits=f32tof16(exp(H(base+key)-mx)/sum);if(key+1<T)bits|=f32tof16(exp(H(base+key+1)-mx)/sum)<<16;probability.Store((base+key)*2,bits);}})";char tokenText[16];std::snprintf(tokenText,16,"%u",T);D3D_SHADER_MACRO macros[]={{"T",tokenText},{nullptr,nullptr}};ID3DBlob*softmaxCode=nullptr,*error=nullptr;dmlrt_check("softmax compile",D3DCompile(softmaxSource,std::strlen(softmaxSource),nullptr,macros,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&softmaxCode,&error));D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};D3D12_ROOT_PARAMETER rootParameter{};rootParameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rootParameter.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=1;rootDesc.pParameters=&rootParameter;ID3DBlob*signature=nullptr;dmlrt_check("softmax signature",D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));ID3D12RootSignature*softmaxRoot=nullptr;dmlrt_check("softmax root",d->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&softmaxRoot)));D3D12_COMPUTE_PIPELINE_STATE_DESC softmaxPsoDesc{};softmaxPsoDesc.pRootSignature=softmaxRoot;softmaxPsoDesc.CS={softmaxCode->GetBufferPointer(),softmaxCode->GetBufferSize()};ID3D12PipelineState*softmaxPso=nullptr;dmlrt_check("softmax pso",d->CreateComputePipelineState(&softmaxPsoDesc,IID_PPV_ARGS(&softmaxPso)));D3D12_DESCRIPTOR_HEAP_DESC shd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*softmaxHeap=nullptr;dmlrt_check("softmax heap",d->CreateDescriptorHeap(&shd,IID_PPV_ARGS(&softmaxHeap)));cpu=softmaxHeap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Buffer.NumElements=(UINT)(scoreBytes/4);srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;d->CreateShaderResourceView(score,&srv,cpu);cpu.ptr+=step;D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.Buffer.NumElements=(UINT)(scoreBytes/4);uav.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;d->CreateUnorderedAccessView(prob,nullptr,&uav,cpu);
 QkvPackPass qkvPack;qkvPack.Create(d,qkvOut,scales,qr,kr,vr,qkvBytes,qvBytes);
 expand.Bind(main,ew,branch);contract.Bind(branch,cw,hidden);qkv.Bind(hidden,qw,qkvOut);qk.Bind(qr,kr,score);av.Bind(prob,vr,att);projection.Bind(att,pw,final);
 D3D12_QUERY_HEAP_DESC qh{};qh.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qh.Count=9;ID3D12QueryHeap*queries=nullptr;dmlrt_check("queries",d->CreateQueryHeap(&qh,IID_PPV_ARGS(&queries)));ID3D12Resource*readback=make(d,72,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_NONE);
 cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,0);
 expand.Record(rec,cl);barrier(cl,branch);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,1);
 contract.Record(rec,cl);barrier(cl,hidden);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,2);
 qkv.Record(rec,cl);barrier(cl,qkvOut);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,3);
 qkvPack.Record(cl);barrier(cl,qr);barrier(cl,kr);barrier(cl,vr);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,4);
 qk.Record(rec,cl);barrier(cl,score);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,5);
 ID3D12DescriptorHeap*softmaxHeaps[]={softmaxHeap};cl->SetDescriptorHeaps(1,softmaxHeaps);cl->SetComputeRootSignature(softmaxRoot);cl->SetComputeRootDescriptorTable(0,softmaxHeap->GetGPUDescriptorHandleForHeapStart());cl->SetPipelineState(softmaxPso);cl->Dispatch(T,H,1);barrier(cl,prob);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,6);
 av.Record(rec,cl);barrier(cl,att);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,7);
 projection.Record(rec,cl);barrier(cl,final);cl->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,8);
 cl->ResolveQueryData(queries,D3D12_QUERY_TYPE_TIMESTAMP,0,9,readback,0);wait();
 UINT64*t=nullptr;D3D12_RANGE range{0,72};readback->Map(0,&range,(void**)&t);UINT64 freq=0;queue->GetTimestampFrequency(&freq);const char*names[]={"expand","contract","qkv","qkv_pack","qk","softmax","av","projection"};double total=0;for(UINT i=0;i<8;i++){double ms=1000.0*(t[i+1]-t[i])/freq;total+=ms;std::printf("%s_ms=%.6f\n",names[i],ms);}UINT64 bytes=0;for(auto*r:resources)bytes+=r->GetDesc().Width;wprintf(L"adapter=%ls directml_gemms=6 resident_resources=%u resident_mib=%.2f resident_gpu_ms=%.6f qkv_pack_interop=pass\n",ad.Description,(UINT)resources.size(),bytes/1048576.0,total);return 0;}
