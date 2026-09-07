#pragma once
#include <d3d12.h>
#include <stdexcept>
// One workspace per serially executed graph. Must outlive all borrowing layers.
// State tracks command recording order, not whether a particular layer ran before.
class NativeMatrixWorkspace {
 ID3D12Device*device{};UINT64 capacity{};
 static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("matrix workspace allocation failed");}
 ID3D12Resource*Buffer(UINT64 bytes){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=bytes;r.Height=1;r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource*p=nullptr;ck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&p)));return p;}
public:
 ID3D12Resource*packed{},*qkv{},*norm{};bool packed_readable{},qkv_readable{},norm_readable{};
 NativeMatrixWorkspace()=default;NativeMatrixWorkspace(const NativeMatrixWorkspace&)=delete;
 ~NativeMatrixWorkspace(){if(packed)packed->Release();if(qkv)qkv->Release();if(norm)norm->Release();if(device)device->Release();}
 void Create(ID3D12Device*d,UINT64 values){if(device||!d||!values)throw std::runtime_error("matrix workspace contract");device=d;device->AddRef();capacity=values;packed=Buffer(values*2);qkv=Buffer(values*12);norm=Buffer(values*6);}
 void Validate(ID3D12Device*d,UINT64 values)const{if(d!=device||values>capacity||!packed||!qkv)throw std::runtime_error("matrix workspace capacity/device mismatch");}
};
