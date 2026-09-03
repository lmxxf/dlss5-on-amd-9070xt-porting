#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>

static void check(const char *name, HRESULT hr) {
    if (FAILED(hr)) { std::fprintf(stderr, "%s: 0x%08lx\n", name, hr); ExitProcess(1); }
}

int main() {
    IDXGIFactory6 *factory = nullptr;
    check("CreateDXGIFactory2", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    IDXGIAdapter1 *adapter = nullptr;
    DXGI_ADAPTER_DESC1 desc{};
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1 *candidate = nullptr;
        if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
        candidate->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && wcsstr(desc.Description, L"AMD")) {
            adapter = candidate;
            break;
        }
        candidate->Release();
    }
    if (!adapter) return 2;
    ID3D12Device *device = nullptr;
    check("D3D12CreateDevice", D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device)));

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4{};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{};
    HRESULT sm_result = E_INVALIDARG;
    for (unsigned minor = 9; minor <= 9; --minor) {
        shader_model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0x60 + minor);
        sm_result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
            &shader_model, sizeof(shader_model));
        if (SUCCEEDED(sm_result) || minor == 0) break;
    }
    check("OPTIONS1", device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,
        &options1, sizeof(options1)));
    check("OPTIONS4", device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4,
        &options4, sizeof(options4)));

    wprintf(L"adapter=%ls\n", desc.Description);
    std::printf("shader_model_query_hr=0x%08lx\n", sm_result);
    std::printf("highest_shader_model=6.%u\n", unsigned(shader_model.HighestShaderModel & 0xf));
    std::printf("wave_ops=%s\n", options1.WaveOps ? "true" : "false");
    std::printf("wave_lane_min=%u\n", options1.WaveLaneCountMin);
    std::printf("wave_lane_max=%u\n", options1.WaveLaneCountMax);
    std::printf("total_lane_count=%u\n", options1.TotalLaneCount);
    std::printf("native_16bit_shader_ops=%s\n",
        options4.Native16BitShaderOpsSupported ? "true" : "false");
    return 0;
}
