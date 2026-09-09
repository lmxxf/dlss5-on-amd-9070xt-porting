#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <initializer_list>
#include <cstring>

#ifdef DLSS5_AGILITY_PROBE
#ifndef DLSS5_AGILITY_VERSION
#define DLSS5_AGILITY_VERSION 721
#endif
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = DLSS5_AGILITY_VERSION;
__declspec(dllexport) const char *D3D12SDKPath = ".\\D3D12\\";
}
#endif

// Read-only capability queries on a separate device using the system runtime.
// --experimental opts in only for this independent process, before creation.
// No machine configuration is changed by this executable.
int main(int argc,char **argv) {
    if(argc>1&&std::strcmp(argv[1],"--experimental")==0){
        const IID features[]={
            {0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}},
#if defined(DLSS5_AGILITY_VERSION) && DLSS5_AGILITY_VERSION == 717
            {0x384748be,0xcca5,0x471e,{0xa1,0x25,0x5c,0xc9,0x97,0xe0,0x4d,0x39}},
#endif
        };
        HRESULT enabled=D3D12EnableExperimentalFeatures(UINT(sizeof(features)/sizeof(features[0])),features,nullptr,nullptr);
        std::printf("experimental_hr=%08x\n",unsigned(enabled));if(FAILED(enabled))return 4;
    }
    IDXGIFactory6 *factory=nullptr;
    HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(hr)){std::printf("factory_hr=%08x\n",unsigned(hr));return 1;}
    IDXGIAdapter1 *adapter=nullptr;
    for(UINT i=0;;i++){
        IDXGIAdapter1 *candidate=nullptr;
        if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&candidate))==DXGI_ERROR_NOT_FOUND)break;
        if(!candidate)continue;
        DXGI_ADAPTER_DESC1 desc{};candidate->GetDesc1(&desc);
        if(desc.VendorId==0x1002&&!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=candidate;break;}
        candidate->Release();
    }
    factory->Release();if(!adapter)return 2;
    ID3D12Device *device=nullptr;
    hr=D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device));
    adapter->Release();std::printf("device_hr=%08x\n",unsigned(hr));
    if(HMODULE core=GetModuleHandleW(L"D3D12Core.dll")){wchar_t path[MAX_PATH]{};GetModuleFileNameW(core,path,MAX_PATH);std::printf("core_path=%ls\n",path);}
    if(FAILED(hr))return 3;
    for(UINT requested: {0x6au,0x69u,0x68u,0x67u,0x66u}){
        D3D12_FEATURE_DATA_SHADER_MODEL model{static_cast<D3D_SHADER_MODEL>(requested)};
        hr=device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model));
        std::printf("requested=%02x hr=%08x returned=%02x\n",requested,unsigned(hr),unsigned(model.HighestShaderModel));
    }
    // Agility 1.717.1: experimental options feature 9, one 32-bit tier field.
#if defined(DLSS5_AGILITY_VERSION) && DLSS5_AGILITY_VERSION == 717
    UINT coop=0;hr=device->CheckFeatureSupport(static_cast<D3D12_FEATURE>(9),&coop,sizeof(coop));
    std::printf("cooperative_vector_hr=%08x tier=%x\n",unsigned(hr),coop);
#endif
    // Agility 1.721.3: feature 77, one 32-bit tier field; 0x10 means tier 1.0.
    UINT tier=0;hr=device->CheckFeatureSupport(static_cast<D3D12_FEATURE>(77),&tier,sizeof(tier));
    std::printf("linalg_hr=%08x tier=%x\n",unsigned(hr),tier);
    device->Release();return 0;
}
