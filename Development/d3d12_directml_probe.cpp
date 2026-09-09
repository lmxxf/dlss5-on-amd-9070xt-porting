#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#define DML_TARGET_VERSION_USE_LATEST
#ifndef _Maybenull_
#define _Maybenull_
#endif
#include <DirectML.h>
#include <cstdio>
static void ck(const char*n,HRESULT h){if(FAILED(h)){std::fprintf(stderr,"%s=0x%08lx\n",n,h);ExitProcess(1);}}
using CreateFn=HRESULT(WINAPI*)(ID3D12Device*,DML_CREATE_DEVICE_FLAGS,REFIID,void**);
static const GUID iid_idml_device={0x6dbd6437,0x96fd,0x423f,{0xa9,0x8c,0xae,0x5e,0x7c,0x2a,0x57,0x3f}};
int main(){IDXGIFactory6*f=nullptr;ck("factory",CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));IDXGIAdapter1*a=nullptr;DXGI_ADAPTER_DESC1 ad{};for(UINT i=0;;i++){IDXGIAdapter1*x=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&x))==DXGI_ERROR_NOT_FOUND)break;x->GetDesc1(&ad);if(!(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&wcsstr(ad.Description,L"AMD")){a=x;break;}x->Release();}if(!a)return 2;ID3D12Device*d=nullptr;ck("d3d12",D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));HMODULE m=LoadLibraryW(L"DirectML.dll");if(!m){std::fprintf(stderr,"LoadLibrary DirectML=%lu\n",GetLastError());return 3;}auto create=(CreateFn)GetProcAddress(m,"DMLCreateDevice");if(!create){std::fprintf(stderr,"DMLCreateDevice missing\n");return 3;}IDMLDevice*ml=nullptr;ck("DMLCreateDevice",create(d,DML_CREATE_DEVICE_FLAG_NONE,iid_idml_device,(void**)&ml));DML_FEATURE_LEVEL levels[]={DML_FEATURE_LEVEL_6_4,DML_FEATURE_LEVEL_6_3,DML_FEATURE_LEVEL_6_2,DML_FEATURE_LEVEL_6_1,DML_FEATURE_LEVEL_6_0,DML_FEATURE_LEVEL_5_1,DML_FEATURE_LEVEL_5_0};DML_FEATURE_QUERY_FEATURE_LEVELS q{ARRAYSIZE(levels),levels};DML_FEATURE_DATA_FEATURE_LEVELS result{};ck("CheckFeatureSupport",ml->CheckFeatureSupport(DML_FEATURE_FEATURE_LEVELS,sizeof(q),&q,sizeof(result),&result));wprintf(L"adapter=%ls directml_dll=%ls max_feature_level=0x%04x\n",ad.Description,L"C:\\Windows\\System32\\DirectML.dll",result.MaxSupportedFeatureLevel);return 0;}
