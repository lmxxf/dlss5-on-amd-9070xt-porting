#pragma once
#include "native_pinned_resource.h"
#include <d3d12.h>
#include <stdexcept>
#include <cstdlib>
#include <cwchar>
// One workspace per serially executed graph. Must outlive all borrowing layers.
// State tracks command recording order, not whether a particular layer ran before.
class NativeMatrixWorkspace {
 ID3D12Device*device{};UINT64 capacity{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("matrix workspace allocation failed");}
 ID3D12Resource*Buffer(UINT64 bytes){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=bytes;r.Height=1;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*p=nullptr;ck(NativeCreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&p)));return p;}
public:
 ID3D12Resource*packed{},*qkv{},*norm{},*result[3]{},*scratch[2]{};bool packed_readable{},qkv_readable{},norm_readable{},result_readable[3]{},scratch_readable[2]{};
 NativeMatrixWorkspace()=default;NativeMatrixWorkspace(const NativeMatrixWorkspace&)=delete;
 ~NativeMatrixWorkspace(){if(packed)packed->Release();if(qkv)qkv->Release();if(norm)norm->Release();for(auto*r:result)if(r)r->Release();for(auto*r:scratch)if(r)r->Release();if(device)device->Release();}
 void Create(ID3D12Device*d,UINT64 values){if(device||!d||!values)throw std::runtime_error("matrix workspace contract");device=d;device->AddRef();capacity=values;packed=Buffer(values*2);qkv=Buffer(values*12);norm=Buffer(values*6);
  // Optional block-local activation scratch shared by every serially executed multihead block.
  if(const wchar_t*ss=_wgetenv(L"DLSS5_TEST_SHARED_SCRATCH")){if(wcscmp(ss,L"0")&&wcscmp(ss,L"1"))throw std::runtime_error("invalid shared scratch flag");if(!wcscmp(ss,L"1")){for(auto&r:result)r=Buffer(values*4);scratch[0]=Buffer(values*16);scratch[1]=Buffer(values*4);}}}
 void Validate(ID3D12Device*d,UINT64 values)const{if(d!=device||values>capacity||!packed||!qkv)throw std::runtime_error("matrix workspace capacity/device mismatch");}
};
