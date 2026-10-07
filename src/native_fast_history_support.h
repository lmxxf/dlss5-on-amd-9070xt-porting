#pragma once
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
namespace NativeFastHistorySupport {
inline DXGI_FORMAT MotionFormat(DXGI_FORMAT f)
{
    if (f == DXGI_FORMAT_R16G16_TYPELESS) return DXGI_FORMAT_R16G16_FLOAT;
    if (f == DXGI_FORMAT_R32G32_TYPELESS) return DXGI_FORMAT_R32G32_FLOAT;
    if (f == DXGI_FORMAT_R16G16B16A16_TYPELESS) return DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (f == DXGI_FORMAT_R32G32B32A32_TYPELESS) return DXGI_FORMAT_R32G32B32A32_FLOAT;
    // The shader reads XY only; extra float channels do not change vector units.
    return f == DXGI_FORMAT_R16G16_FLOAT || f == DXGI_FORMAT_R32G32_FLOAT ||
           f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT ? f : DXGI_FORMAT_UNKNOWN;
}
inline DXGI_FORMAT DepthFormat(DXGI_FORMAT f)
{
    if (f == DXGI_FORMAT_R32_TYPELESS) return DXGI_FORMAT_R32_FLOAT;
    if (f == DXGI_FORMAT_R16_TYPELESS) return DXGI_FORMAT_R16_UNORM;
    if (f == DXGI_FORMAT_R24G8_TYPELESS) return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    if (f == DXGI_FORMAT_R32G8X24_TYPELESS) return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    return f == DXGI_FORMAT_R32_FLOAT || f == DXGI_FORMAT_R16_FLOAT || f == DXGI_FORMAT_R16_UNORM ||
           f == DXGI_FORMAT_R24_UNORM_X8_TYPELESS || f == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS ? f : DXGI_FORMAT_UNKNOWN;
}
inline const char *TextureIssue(const D3D12_RESOURCE_DESC &d)
{
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) return "dimension";
    if (d.DepthOrArraySize != 1) return "array";
    if (d.SampleDesc.Count != 1) return "samples";
    if (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) return "deny-srv";
    // The SRV exposes mip zero only, even when the allocation has a mip chain.
    if (!d.MipLevels) return "mips";
    return nullptr;
}
inline void Check(HRESULT hr, const char *what)
{
    if (FAILED(hr)) throw std::runtime_error(std::string("temporal: ") + what + " HRESULT=" + std::to_string(unsigned(hr)));
}
inline void Transition(ID3D12GraphicsCommandList *cmd, ID3D12Resource *r,
                       D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b,
                       UINT subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
{
    if(a==b) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={r,subresource,a,b};
    cmd->ResourceBarrier(1,&barrier);
}


}
