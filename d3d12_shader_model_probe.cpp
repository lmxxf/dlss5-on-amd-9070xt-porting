#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <initializer_list>

// Read-only capability queries on a separate device using the system runtime.
// Does not enable experimental features or change machine configuration.
int main() {
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
    adapter->Release();std::printf("device_hr=%08x\n",unsigned(hr));if(FAILED(hr))return 3;
    for(UINT requested: {0x6au,0x69u,0x68u,0x67u,0x66u}){
        D3D12_FEATURE_DATA_SHADER_MODEL model{static_cast<D3D_SHADER_MODEL>(requested)};
        hr=device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model));
        std::printf("requested=%02x hr=%08x returned=%02x\n",requested,unsigned(hr),unsigned(model.HighestShaderModel));
    }
    device->Release();return 0;
}
