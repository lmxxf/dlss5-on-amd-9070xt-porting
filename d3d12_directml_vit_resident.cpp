#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#define DML_TARGET_VERSION_USE_LATEST
#ifndef _Maybenull_
#define _Maybenull_
#endif
#include <DirectML.h>
#include "directml_gemm_runtime.h"
#include <cstdio>

static const GUID IID_DML_DEVICE={0x6dbd6437,0x96fd,0x423f,{0xa9,0x8c,0xae,0x5e,0x7c,0x2a,0x57,0x3f}};
static const GUID IID_DML_RECORDER={0xe6857a76,0x2e3e,0x4fdd,{0xbf,0xf4,0x5d,0x2b,0xa1,0x0f,0xb4,0x53}};
using CreateFn=HRESULT(WINAPI*)(ID3D12Device*,DML_CREATE_DEVICE_FLAGS,REFIID,void**);
static D3D12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE t){D3D12_HEAP_PROPERTIES x{};x.Type=t;x.CreationNodeMask=x.VisibleNodeMask=1;return x;}
static D3D12_RESOURCE_DESC bd(UINT64 n,D3D12_RESOURCE_FLAGS f){D3D12_RESOURCE_DESC x{};x.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;x.Width=n;x.Height=1;x.DepthOrArraySize=1;x.MipLevels=1;x.SampleDesc.Count=1;x.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;x.Flags=f;return x;}
static ID3D12Resource* buffer(ID3D12Device*d,UINT64 n){auto h=hp(D3D12_HEAP_TYPE_DEFAULT);auto b=bd(n,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);ID3D12Resource*r=nullptr;dmlrt_check("resource",d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;}
int main(){constexpr UINT T=2160,H=32,D=32;IDXGIFactory6*f=nullptr;dmlrt_check("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));IDXGIAdapter1*a=nullptr;DXGI_ADAPTER_DESC1 ad{};for(UINT i=0;;i++){IDXGIAdapter1*x=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&x))==DXGI_ERROR_NOT_FOUND)break;x->GetDesc1(&ad);if(!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(ad.Description,L"AMD")){a=x;break;}x->Release();}if(!a)return 2;ID3D12Device*dx=nullptr;dmlrt_check("device",D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dx)));HMODULE m=LoadLibraryW(L"DirectML.dll");auto create=(CreateFn)GetProcAddress(m,"DMLCreateDevice");IDMLDevice*dml=nullptr;dmlrt_check("DMLCreateDevice",create(dx,DML_CREATE_DEVICE_FLAG_NONE,IID_DML_DEVICE,(void**)&dml));IDMLCommandRecorder*rec=nullptr;dmlrt_check("recorder",dml->CreateCommandRecorder(IID_DML_RECORDER,(void**)&rec));DmlGemmOperator expand,contract,qkv,qk,av,projection;expand.Create(dml,dx,1,T,1024,4096);contract.Create(dml,dx,1,T,4096,1024);qkv.Create(dml,dx,1,T,1024,3072);qk.Create(dml,dx,H,T,D,T);av.Create(dml,dx,H,T,T,D);projection.Create(dml,dx,1,T,1024,1024);
 D3D12_COMMAND_QUEUE_DESC qd{};ID3D12CommandQueue*q=nullptr;dmlrt_check("queue",dx->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*ca=nullptr;dmlrt_check("allocator",dx->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&ca)));ID3D12GraphicsCommandList*cl=nullptr;dmlrt_check("list",dx->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,ca,nullptr,IID_PPV_ARGS(&cl)));for(auto*g:{&expand,&contract,&qkv,&qk,&av,&projection})g->RecordInitialization(rec,cl);dmlrt_check("close",cl->Close());ID3D12CommandList*lists[]={cl};q->ExecuteCommandLists(1,lists);ID3D12Fence*fe=nullptr;dmlrt_check("fence",dx->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fe)));q->Signal(fe,1);HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);fe->SetEventOnCompletion(1,ev);WaitForSingleObject(ev,INFINITE);
 UINT64 main=UINT64(T)*1024*4,branch=UINT64(T)*4096*4,qkvF32=UINT64(T)*3072*4,qv=UINT64(H)*T*D*2,score=UINT64(H)*T*T*2;ID3D12Resource*resources[]={buffer(dx,main),buffer(dx,branch),buffer(dx,qkvF32),buffer(dx,qv),buffer(dx,qv),buffer(dx,qv),buffer(dx,score),buffer(dx,score),buffer(dx,qv)};UINT64 bytes=main+branch+qkvF32+qv*4+score*2;wprintf(L"adapter=%ls directml_gemms=6 resident_resources=9 resident_mib=%.2f score_mib_each=%.2f initialization=pass\n",ad.Description,bytes/1048576.0,score/1048576.0);return 0;}
