#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, HRESULT hr) {
    if (FAILED(hr)) { std::fprintf(stderr, "%s: 0x%08lx\n", name, hr); ExitProcess(1); }
}
static std::vector<uint8_t> read_file(const wchar_t *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate); if (!f) ExitProcess(2);
    const size_t n=(size_t)f.tellg(); f.seekg(0); std::vector<uint8_t> b(n); f.read((char*)b.data(),n); return b;
}
static D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE t) { D3D12_HEAP_PROPERTIES h{}; h.Type=t; h.CreationNodeMask=h.VisibleNodeMask=1; return h; }
static D3D12_RESOURCE_DESC buffer(UINT64 n, D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=n; d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=flags; return d;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 4) { std::fwprintf(stderr,L"usage: %ls effective.bin input.f32 oracle.f32\n",argv[0]); return 2; }
    const auto weights=read_file(argv[1]), input=read_file(argv[2]), oracle=read_file(argv[3]);
    if (weights.size()!=41220 || input.size()!=256ull*64*32*4 || oracle.size()!=input.size()) return 2;
    constexpr UINT tiles=256, tokens=tiles*64; const UINT64 tensor_bytes=input.size();

    IDXGIFactory6 *factory=nullptr; check("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    IDXGIAdapter1 *adapter=nullptr; DXGI_ADAPTER_DESC1 ad{};
    for(UINT i=0;;++i){IDXGIAdapter1*c=nullptr;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&c))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};c->GetDesc1(&d);if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(d.Description,L"AMD")){adapter=c;ad=d;break;}c->Release();}
    if(!adapter)return 1; ID3D12Device *dev=nullptr; check("device",D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dev)));
    std::wprintf(L"adapter: %ls\n",ad.Description);
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT; ID3D12CommandQueue*q=nullptr;check("queue",dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ID3D12CommandAllocator*alloc=nullptr;check("alloc",dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ID3D12GraphicsCommandList*cmd=nullptr;check("list",dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,nullptr,IID_PPV_ARGS(&cmd)));

    const char shader[] = R"(
ByteAddressBuffer w:register(t0); StructuredBuffer<float> inp:register(t1); StructuredBuffer<float> feat_in:register(t2);
RWStructuredBuffer<float> feat:register(u0); RWStructuredBuffer<float> outp:register(u1);
float W(uint i){return asfloat(w.Load(i*4));}
float fp8(float x){
 if(x==0)return 0; float s=x<0?-1:1;float a=abs(x); if(a<0.015625){return s*round(a*512.0)/512.0;}
 float e=floor(log2(a));e=clamp(e,-6.0,8.0);float m=round((a/exp2(e)-1.0)*8.0);if(m>=8){m=0;e+=1;}float r=exp2(e)*(1.0+m/8.0);return s*min(r,448.0);
}
[numthreads(64,1,1)] void ffn(uint3 id:SV_DispatchThreadID){uint t=id.x;if(t>=16384)return;float h[64];
 [loop]for(uint j=0;j<64;j++){float a=0;[loop]for(uint c=0;c<32;c++)a+=inp[t*32+c]*W(j*32+c);a=clamp(a,-4.0,4.0);h[j]=a*(0.89453125+a*(0.447265625-0.055908203125*abs(a)));}
 [loop]for(uint c=0;c<32;c++){float v=0;[loop]for(uint j=0;j<64;j++)v+=h[j]*W(2048+c*64+j);v+=inp[t*32+c]*W(10241+c);feat[t*32+c]=fp8(v);}}
void qkv(uint token,out float q[16],out float k[16],out float v[16]){[loop]for(uint o=0;o<16;o++){q[o]=k[o]=v[o]=0;[loop]for(uint j=0;j<16;j++){float e=feat_in[token*32+j*2],z=feat_in[token*32+j*2+1];q[o]+=e*W(4096+o*16+j)+z*W(4352+o*16+j);k[o]+=e*W(4608+o*16+j)+z*W(4864+o*16+j);v[o]+=e*W(5120+o*16+j)+z*W(5376+o*16+j);}}}
float cosine_logit(uint query,uint key,float q[16]){float qq=0,kk=0,d=0;float tq[16],tk[16],tv[16];qkv(key,tq,tk,tv);[loop]for(uint i=0;i<16;i++){qq+=q[i]*q[i];kk+=tk[i]*tk[i];d+=q[i]*tk[i];}return d*rsqrt(max(qq,1e-12))*rsqrt(max(kk,1e-12))*W(10240)+W(6144+query*64+key);}
[numthreads(64,1,1)] void attention(uint3 id:SV_DispatchThreadID){uint t=id.x;if(t>=16384)return;uint tile=t/64,query=t%64;float qv[16],kv[16],vv[16];qkv(t,qv,kv,vv);float mx=-3.4e38;[loop]for(uint key=0;key<64;key++)mx=max(mx,cosine_logit(query,key,qv));float den=0,acc[16];[unroll]for(uint i=0;i<16;i++)acc[i]=0;
 [loop]for(uint key=0;key<64;key++){uint kt=tile*64+key;float tq[16],tk[16],tv[16];qkv(kt,tq,tk,tv);float qq=0,kk=0,d=0;[loop]for(uint i=0;i<16;i++){qq+=qv[i]*qv[i];kk+=tk[i]*tk[i];d+=qv[i]*tk[i];}float e=exp((d*rsqrt(max(qq,1e-12))*rsqrt(max(kk,1e-12))*W(10240)+W(6144+query*64+key))-mx);den+=e;[loop]for(uint i=0;i<16;i++)acc[i]+=e*tv[i];}
 [loop]for(uint c=0;c<32;c++){float z=0;[loop]for(uint i=0;i<16;i++)z+=(acc[i]/den)*W(5632+c*16+i);z+=feat_in[t*32+c]*W(10273+c);outp[t*32+c]=fp8(z);}}
)";
    ID3DBlob *b1=nullptr,*b2=nullptr,*err=nullptr;check("compile ffn",D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ffn","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b1,&err));if(err)err->Release();err=nullptr;check("compile attention",D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"attention","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b2,&err));if(err)err->Release();
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,3};D3D12_ROOT_PARAMETER rp{};rp.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rsd{};rsd.NumParameters=1;rsd.pParameters=&rp;ID3DBlob*rsb=nullptr;check("serialize",D3D12SerializeRootSignature(&rsd,D3D_ROOT_SIGNATURE_VERSION_1,&rsb,&err));ID3D12RootSignature*rs=nullptr;check("rootsig",dev->CreateRootSignature(0,rsb->GetBufferPointer(),rsb->GetBufferSize(),IID_PPV_ARGS(&rs)));rsb->Release();
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=rs;pd.CS={b1->GetBufferPointer(),b1->GetBufferSize()};ID3D12PipelineState*p1=nullptr,*p2=nullptr;check("pso1",dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&p1)));pd.CS={b2->GetBufferPointer(),b2->GetBufferSize()};check("pso2",dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&p2)));b1->Release();b2->Release();
    auto hp=heap(D3D12_HEAP_TYPE_UPLOAD),hd=heap(D3D12_HEAP_TYPE_DEFAULT),hr=heap(D3D12_HEAP_TYPE_READBACK);ID3D12Resource *wr=nullptr,*ir=nullptr,*fr=nullptr,*orr=nullptr,*rb=nullptr;auto wd=buffer(weights.size()),td=buffer(tensor_bytes),ud=buffer(tensor_bytes,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);check("wr",dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&wd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&wr)));check("ir",dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&ir)));check("fr",dev->CreateCommittedResource(&hd,D3D12_HEAP_FLAG_NONE,&ud,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&fr)));check("or",dev->CreateCommittedResource(&hd,D3D12_HEAP_FLAG_NONE,&ud,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&orr)));check("rb",dev->CreateCommittedResource(&hr,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));void*m=nullptr;D3D12_RANGE none{0,0};wr->Map(0,&none,&m);memcpy(m,weights.data(),weights.size());wr->Unmap(0,nullptr);ir->Map(0,&none,&m);memcpy(m,input.data(),input.size());ir->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC dhd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,5,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ID3D12DescriptorHeap*dh=nullptr;check("heap",dev->CreateDescriptorHeap(&dhd,IID_PPV_ARGS(&dh)));UINT ds=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto h=dh->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Format=DXGI_FORMAT_R32_TYPELESS;sd.Buffer.NumElements=weights.size()/4;sd.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;dev->CreateShaderResourceView(wr,&sd,h);h.ptr+=ds;sd.Format=DXGI_FORMAT_UNKNOWN;sd.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;sd.Buffer.StructureByteStride=4;sd.Buffer.NumElements=tensor_bytes/4;dev->CreateShaderResourceView(ir,&sd,h);h.ptr+=ds;dev->CreateShaderResourceView(fr,&sd,h);h.ptr+=ds;D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=tensor_bytes/4;uv.Buffer.StructureByteStride=4;dev->CreateUnorderedAccessView(fr,nullptr,&uv,h);h.ptr+=ds;dev->CreateUnorderedAccessView(orr,nullptr,&uv,h);
    ID3D12DescriptorHeap*heaps[]={dh};cmd->SetDescriptorHeaps(1,heaps);cmd->SetComputeRootSignature(rs);cmd->SetComputeRootDescriptorTable(0,dh->GetGPUDescriptorHandleForHeapStart());cmd->SetPipelineState(p1);cmd->Dispatch((tokens+63)/64,1,1);D3D12_RESOURCE_BARRIER bs[2]{};bs[0].Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;bs[0].UAV.pResource=fr;bs[1].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;bs[1].Transition.pResource=fr;bs[1].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;bs[1].Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;bs[1].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;cmd->ResourceBarrier(2,bs);cmd->SetPipelineState(p2);cmd->Dispatch((tokens+63)/64,1,1);D3D12_RESOURCE_BARRIER bo{};bo.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;bo.Transition.pResource=orr;bo.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;bo.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;bo.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;cmd->ResourceBarrier(1,&bo);cmd->CopyBufferRegion(rb,0,orr,0,tensor_bytes);check("close",cmd->Close());
    LARGE_INTEGER fq,t0,t1;QueryPerformanceFrequency(&fq);QueryPerformanceCounter(&t0);ID3D12CommandList*ls[]={cmd};q->ExecuteCommandLists(1,ls);ID3D12Fence*f=nullptr;check("fence",dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));q->Signal(f,1);HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);f->SetEventOnCompletion(1,ev);WaitForSingleObject(ev,INFINITE);QueryPerformanceCounter(&t1);CloseHandle(ev);
    D3D12_RANGE all{0,(SIZE_T)tensor_bytes};rb->Map(0,&all,&m);const float*got=(const float*)m,*want=(const float*)oracle.data();double ae=0,se=0;size_t exact=0,n=tensor_bytes/4;for(size_t i=0;i<n;i++){double d=got[i]-want[i];ae+=fabs(d);se+=d*d;exact+=got[i]==want[i];}rb->Unmap(0,&none);std::printf("submit_to_fence_ms: %.3f\nMAE: %.9f\nRMSE: %.9f\nexact: %.6f\n",1000.0*(t1.QuadPart-t0.QuadPart)/fq.QuadPart,ae/n,sqrt(se/n),(double)exact/n);
    return 0;
}
