#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <vector>
#include <stdexcept>
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion=721;
__declspec(dllexport) const char* D3D12SDKPath=".\\D3D12\\";
}
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("probe initialization failed");}
int wmain(int argc,wchar_t**argv){try{
 if(argc<2)return 2;
 ID3D12Debug*debug=nullptr;HRESULT dh=D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
 printf("debug_interface=%08x\n",unsigned(dh));if(SUCCEEDED(dh)){debug->EnableDebugLayer();debug->Release();}
 const IID experimental={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};
 check(D3D12EnableExperimentalFeatures(1,&experimental,nullptr,nullptr));
 IDXGIFactory6*f=nullptr;check(CreateDXGIFactory1(IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002){check(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();break;}a->Release();}
 if(!d)throw std::runtime_error("AMD missing");
 ID3D12InfoQueue*info=nullptr;HRESULT ih=d->QueryInterface(IID_PPV_ARGS(&info));printf("info_queue=%08x\n",unsigned(ih));
 D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2}};
 D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,5};
 D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=p;
 ID3DBlob*blob=nullptr,*error=nullptr;check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ID3D12RootSignature*root=nullptr;check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
 for(int i=1;i<argc;i++){
  ID3DBlob*shader=nullptr;check(D3DReadFileToBlob(argv[i],&shader));if(info)info->ClearStoredMessages();
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};ID3D12PipelineState*pso=nullptr;
  HRESULT hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso));printf("candidate=%d pso=%08x bytes=%llu\n",i,unsigned(hr),(unsigned long long)shader->GetBufferSize());
  if(info)for(UINT64 j=0;j<info->GetNumStoredMessages();j++){SIZE_T size=0;check(info->GetMessage(j,nullptr,&size));std::vector<unsigned char>storage(size);auto*m=reinterpret_cast<D3D12_MESSAGE*>(storage.data());check(info->GetMessage(j,m,&size));printf("message id=%u severity=%u: %s\n",unsigned(m->ID),unsigned(m->Severity),m->pDescription);}
  if(pso)pso->Release();shader->Release();
 }
 if(info)info->Release();root->Release();d->Release();f->Release();return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
