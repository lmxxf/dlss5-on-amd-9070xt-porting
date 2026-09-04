#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#define DML_TARGET_VERSION_USE_LATEST
#ifndef _Maybenull_
#define _Maybenull_
#endif
#include <DirectML.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>

static void ck(const char* n,HRESULT h){if(FAILED(h)){std::fprintf(stderr,"%s=0x%08lx\n",n,(unsigned long)h);ExitProcess(1);}}
static const GUID iid_dev={0x6dbd6437,0x96fd,0x423f,{0xa9,0x8c,0xae,0x5e,0x7c,0x2a,0x57,0x3f}};
static const GUID iid_op={0x26caae7a,0x3081,0x4633,{0x95,0x81,0x22,0x6f,0xbe,0x57,0x69,0x5d}};
static const GUID iid_compiled={0x6b15e56a,0xbf5c,0x4902,{0x92,0xd8,0xda,0x3a,0x65,0x0a,0xfe,0xa4}};
static const GUID iid_init={0x427c1113,0x435c,0x469c,{0x86,0x76,0x4d,0x5d,0xd0,0x72,0xf8,0x13}};
static const GUID iid_table={0x29c687dc,0xde74,0x4e3b,{0xab,0x00,0x11,0x68,0xf2,0xfc,0x3c,0xfc}};
static const GUID iid_rec={0xe6857a76,0x2e3e,0x4fdd,{0xbf,0xf4,0x5d,0x2b,0xa1,0x0f,0xb4,0x53}};
using CreateFn=HRESULT(WINAPI*)(ID3D12Device*,DML_CREATE_DEVICE_FLAGS,REFIID,void**);

static ID3D12Resource* buffer(ID3D12Device*d,UINT64 bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
 D3D12_HEAP_PROPERTIES hp{};hp.Type=heap;hp.CreationNodeMask=hp.VisibleNodeMask=1;
 D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=std::max<UINT64>(bytes,4);rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=flags;
 ID3D12Resource*r=nullptr;ck("CreateCommittedResource",d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
static UINT64 fp16_bytes(UINT64 elements){return (elements*2+3)&~UINT64(3);}
static UINT16 f32_to_f16(float value){UINT32 x;std::memcpy(&x,&value,4);UINT32 s=(x>>16)&0x8000,m=x&0x7fffff;int e=int((x>>23)&255)-127+15;if(e<=0){if(e<-10)return (UINT16)s;m=(m|0x800000)>>(1-e);return (UINT16)(s+((m+0x1000)>>13));}if(e>=31)return (UINT16)(s|0x7c00);return (UINT16)(s|(UINT32(e)<<10)|((m+0x1000)>>13));}
static DML_BINDING_PROPERTIES binding_properties(IDMLDispatchable*x){
 DML_BINDING_PROPERTIES p{};void**vtable=*(void***)x;
 using Fn=void(WINAPI*)(IDMLDispatchable*,DML_BINDING_PROPERTIES*);
 ((Fn)vtable[8])(x,&p);return p;
}
static void submit_wait(ID3D12CommandQueue*q,ID3D12GraphicsCommandList*l,ID3D12CommandAllocator*a,ID3D12Fence*f,UINT64&fv,HANDLE ev){
 ck("Close",l->Close());ID3D12CommandList*ls[]={l};q->ExecuteCommandLists(1,ls);ck("Signal",q->Signal(f,++fv));if(f->GetCompletedValue()<fv){ck("SetEvent",f->SetEventOnCompletion(fv,ev));WaitForSingleObject(ev,INFINITE);}ck("AllocatorReset",a->Reset());ck("ListReset",l->Reset(a,nullptr));
}
int main(int argc,char**argv){
 const UINT M=argc>1?std::strtoul(argv[1],nullptr,10):2160,K=argc>2?std::strtoul(argv[2],nullptr,10):1024,N=argc>3?std::strtoul(argv[3],nullptr,10):4096,iters=argc>4?std::strtoul(argv[4],nullptr,10):50;const bool fileMode=argc==8;
 IDXGIFactory6*fac=nullptr;ck("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&fac)));IDXGIAdapter1*adp=nullptr;DXGI_ADAPTER_DESC1 ad{};
 for(UINT i=0;;++i){IDXGIAdapter1*x=nullptr;if(fac->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&x))==DXGI_ERROR_NOT_FOUND)break;x->GetDesc1(&ad);if(!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(ad.Description,L"AMD")){adp=x;break;}x->Release();}if(!adp)return 2;
 ID3D12Device*d=nullptr;ck("D3D12CreateDevice",D3D12CreateDevice(adp,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
 D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*q=nullptr;ck("CreateQueue",d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*ca=nullptr;ck("CreateAllocator",d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&ca)));ID3D12GraphicsCommandList*cl=nullptr;ck("CreateList",d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,ca,nullptr,IID_PPV_ARGS(&cl)));ID3D12Fence*fence=nullptr;ck("CreateFence",d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 fv=0;
 HMODULE mod=LoadLibraryW(L"DirectML.dll");if(!mod)return 3;auto create=(CreateFn)GetProcAddress(mod,"DMLCreateDevice");IDMLDevice*ml=nullptr;ck("DMLCreateDevice",create(d,DML_CREATE_DEVICE_FLAG_NONE,iid_dev,(void**)&ml));
 UINT asz[4]={1,1,M,K},bsz[4]={1,1,K,N},osz[4]={1,1,M,N};
 DML_BUFFER_TENSOR_DESC ab{DML_TENSOR_DATA_TYPE_FLOAT16,DML_TENSOR_FLAG_NONE,4,asz,nullptr,fp16_bytes(UINT64(M)*K),0};
 DML_BUFFER_TENSOR_DESC bb{DML_TENSOR_DATA_TYPE_FLOAT16,DML_TENSOR_FLAG_NONE,4,bsz,nullptr,fp16_bytes(UINT64(K)*N),0};
 DML_BUFFER_TENSOR_DESC ob{DML_TENSOR_DATA_TYPE_FLOAT16,DML_TENSOR_FLAG_NONE,4,osz,nullptr,fp16_bytes(UINT64(M)*N),0};
 DML_TENSOR_DESC at{DML_TENSOR_TYPE_BUFFER,&ab},bt{DML_TENSOR_TYPE_BUFFER,&bb},ot{DML_TENSOR_TYPE_BUFFER,&ob};
 DML_GEMM_OPERATOR_DESC gd{&at,&bt,nullptr,&ot,DML_MATRIX_TRANSFORM_NONE,DML_MATRIX_TRANSFORM_NONE,1.0f,0.0f,nullptr};DML_OPERATOR_DESC od{DML_OPERATOR_GEMM,&gd};
 IDMLOperator*op=nullptr;ck("CreateOperator",ml->CreateOperator(&od,iid_op,(void**)&op));IDMLCompiledOperator*co=nullptr;ck("CompileOperator",ml->CompileOperator(op,DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION,iid_compiled,(void**)&co));
 IDMLCompiledOperator*ops[]={co};IDMLOperatorInitializer*init=nullptr;ck("CreateInitializer",ml->CreateOperatorInitializer(1,ops,iid_init,(void**)&init));
 // Invoke the struct-returning COM method explicitly: MinGW's generated member-call ABI is incompatible here.
 DML_BINDING_PROPERTIES ip=binding_properties(init),ep=binding_properties(co);const UINT descs=std::max<UINT>(1,std::max(ip.RequiredDescriptorCount,ep.RequiredDescriptorCount));const UINT64 tmpBytes=std::max(ip.TemporaryResourceSize,ep.TemporaryResourceSize),persistBytes=ep.PersistentResourceSize;
 std::printf("bindings init_desc=%u exec_desc=%u temp=%llu/%llu persistent=%llu selected_desc=%u\n",ip.RequiredDescriptorCount,ep.RequiredDescriptorCount,(unsigned long long)ip.TemporaryResourceSize,(unsigned long long)ep.TemporaryResourceSize,(unsigned long long)ep.PersistentResourceSize,descs);
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=descs;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ID3D12DescriptorHeap*heap=nullptr;ck("CreateHeap",d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));ID3D12DescriptorHeap*heaps[]={heap};cl->SetDescriptorHeaps(1,heaps);
 DML_BINDING_TABLE_DESC td{init,heap->GetCPUDescriptorHandleForHeapStart(),heap->GetGPUDescriptorHandleForHeapStart(),descs};IDMLBindingTable*table=nullptr;ck("CreateBindingTable",ml->CreateBindingTable(&td,iid_table,(void**)&table));
 ID3D12Resource*tmp=tmpBytes?buffer(d,tmpBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS):nullptr,*persist=persistBytes?buffer(d,persistBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS):nullptr;
 if(ip.TemporaryResourceSize){DML_BUFFER_BINDING b{tmp,0,tmpBytes};DML_BINDING_DESC x{DML_BINDING_TYPE_BUFFER,&b};table->BindTemporaryResource(&x);}if(persistBytes){DML_BUFFER_BINDING b{persist,0,persistBytes};DML_BINDING_DESC x{DML_BINDING_TYPE_BUFFER,&b};table->BindOutputs(1,&x);}
 IDMLCommandRecorder*rec=nullptr;ck("CreateRecorder",ml->CreateCommandRecorder(iid_rec,(void**)&rec));rec->RecordDispatch(cl,init,table);submit_wait(q,cl,ca,fence,fv,ev);
 td.Dispatchable=co;ck("TableReset",table->Reset(&td));cl->SetDescriptorHeaps(1,heaps);if(ep.TemporaryResourceSize){DML_BUFFER_BINDING b{tmp,0,tmpBytes};DML_BINDING_DESC x{DML_BINDING_TYPE_BUFFER,&b};table->BindTemporaryResource(&x);}if(persistBytes){DML_BUFFER_BINDING b{persist,0,persistBytes};DML_BINDING_DESC x{DML_BINDING_TYPE_BUFFER,&b};table->BindPersistentResource(&x);}
 ID3D12Resource*A=buffer(d,ab.TotalTensorSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),*B=buffer(d,bb.TotalTensorSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),*O=buffer(d,ob.TotalTensorSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
 ID3D12Resource*upA=buffer(d,ab.TotalTensorSizeInBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ),*upB=buffer(d,bb.TotalTensorSizeInBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);UINT logK=0;for(UINT z=K;z>1;z>>=1)++logK;if(!fileMode&&((UINT64(1)<<logK)!=K||logK>14)){std::fprintf(stderr,"K must be a power of two <= 16384 for the exact-value check\n");return 5;}UINT16 reciprocalK=UINT16((15-logK)<<10);void*p=nullptr;upA->Map(0,nullptr,&p);if(fileMode){std::ifstream fi(argv[5],std::ios::binary|std::ios::ate);if(!fi||UINT64(fi.tellg())!=UINT64(M)*K*4)return 6;fi.seekg(0);std::vector<float>x(UINT64(M)*K);fi.read((char*)x.data(),x.size()*4);for(UINT64 i=0;i<x.size();i++)((UINT16*)p)[i]=f32_to_f16(x[i]);}else std::fill_n((UINT16*)p,UINT64(M)*K,reciprocalK);upA->Unmap(0,nullptr);upB->Map(0,nullptr,&p);if(fileMode){std::ifstream fw(argv[6],std::ios::binary|std::ios::ate);if(!fw||UINT64(fw.tellg())!=UINT64(K)*N*2)return 6;fw.seekg(0);fw.read((char*)p,UINT64(K)*N*2);}else std::fill_n((UINT16*)p,UINT64(K)*N,UINT16(0x3c00));upB->Unmap(0,nullptr);cl->CopyBufferRegion(A,0,upA,0,ab.TotalTensorSizeInBytes);cl->CopyBufferRegion(B,0,upB,0,bb.TotalTensorSizeInBytes);D3D12_RESOURCE_BARRIER bars[2]{};for(int i=0;i<2;i++){bars[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;bars[i].Transition.pResource=i?B:A;bars[i].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;bars[i].Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;bars[i].Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}cl->ResourceBarrier(2,bars);submit_wait(q,cl,ca,fence,fv,ev);cl->SetDescriptorHeaps(1,heaps);
 DML_BUFFER_BINDING ibs[2]={{A,0,ab.TotalTensorSizeInBytes},{B,0,bb.TotalTensorSizeInBytes}};DML_BINDING_DESC ids[3]={{DML_BINDING_TYPE_BUFFER,&ibs[0]},{DML_BINDING_TYPE_BUFFER,&ibs[1]},{DML_BINDING_TYPE_NONE,nullptr}};table->BindInputs(3,ids);DML_BUFFER_BINDING obind{O,0,ob.TotalTensorSizeInBytes};DML_BINDING_DESC odesc{DML_BINDING_TYPE_BUFFER,&obind};table->BindOutputs(1,&odesc);
 rec->RecordDispatch(cl,co,table);submit_wait(q,cl,ca,fence,fv,ev);cl->SetDescriptorHeaps(1,heaps);
 D3D12_QUERY_HEAP_DESC qhdesc{};qhdesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qhdesc.Count=2;ID3D12QueryHeap*qh=nullptr;ck("QueryHeap",d->CreateQueryHeap(&qhdesc,IID_PPV_ARGS(&qh)));ID3D12Resource*qr=buffer(d,16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 cl->EndQuery(qh,D3D12_QUERY_TYPE_TIMESTAMP,0);for(UINT i=0;i<iters;i++)rec->RecordDispatch(cl,co,table);cl->EndQuery(qh,D3D12_QUERY_TYPE_TIMESTAMP,1);cl->ResolveQueryData(qh,D3D12_QUERY_TYPE_TIMESTAMP,0,2,qr,0);submit_wait(q,cl,ca,fence,fv,ev);UINT64*t=nullptr;D3D12_RANGE rr{0,16};ck("MapQuery",qr->Map(0,&rr,(void**)&t));UINT64 freq=0;ck("Frequency",q->GetTimestampFrequency(&freq));double ms=1000.0*double(t[1]-t[0])/double(freq)/iters;qr->Unmap(0,nullptr);
 UINT64 readBytes=fileMode?ob.TotalTensorSizeInBytes:2;ID3D12Resource*rb=buffer(d,readBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_RESOURCE_BARRIER bo{};bo.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;bo.Transition.pResource=O;bo.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;bo.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;bo.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;cl->ResourceBarrier(1,&bo);cl->CopyBufferRegion(rb,0,O,0,readBytes);submit_wait(q,cl,ca,fence,fv,ev);UINT16*first=nullptr;D3D12_RANGE one{0,(SIZE_T)readBytes};rb->Map(0,&one,(void**)&first);if(fileMode)std::ofstream(argv[7],std::ios::binary).write((char*)first,readBytes);
 double tflops=(2.0*double(M)*K*N)/(ms*1e9);wprintf(L"adapter=%ls shape=%ux%u x %ux%u iterations=%u gpu_ms=%.6f tflops=%.3f first_fp16=0x%04x temp_mib=%.2f persistent_mib=%.2f\n",ad.Description,M,K,K,N,iters,ms,tflops,*first,tmpBytes/1048576.0,persistBytes/1048576.0);return fileMode||*first==0x3c00?0:4;
}
