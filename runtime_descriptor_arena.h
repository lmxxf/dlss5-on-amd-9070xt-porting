#pragma once
#include <d3d12.h>
#include <atomic>
#include <unordered_map>

// Original heap objects remain allocation identities. No fake COM objects are
// passed to D3D12. Only native-device shader-visible CBV/SRV/UAV heaps join.
struct RuntimeDescriptorArena {
    SRWLOCK lock = SRWLOCK_INIT;
    std::atomic<bool> enabled{false};
    ID3D12Device *device{};
    ID3D12DescriptorHeap *heap{};
    UINT capacity{}, used{}, stride{};
    std::unordered_map<ID3D12DescriptorHeap *, UINT> offsets;
};
inline RuntimeDescriptorArena runtime_descriptor_arena;

inline HRESULT runtime_enable_descriptor_arena(ID3D12Device *device, UINT capacity=65536) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,capacity,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    ID3D12DescriptorHeap *heap=nullptr;
    HRESULT hr=device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap));
    if(FAILED(hr))return hr;
    auto &a=runtime_descriptor_arena;
    AcquireSRWLockExclusive(&a.lock);
    if(a.heap){ReleaseSRWLockExclusive(&a.lock);heap->Release();return E_UNEXPECTED;}
    a.device=device;a.heap=heap;a.capacity=capacity;a.stride=device->GetDescriptorHandleIncrementSize(desc.Type);
    ReleaseSRWLockExclusive(&a.lock);
    a.enabled.store(true,std::memory_order_release);
    return S_OK;
}

inline HRESULT runtime_create_descriptor_heap(ID3D12Device *device,const D3D12_DESCRIPTOR_HEAP_DESC *desc,REFIID iid,void **output) {
    HRESULT hr=device->CreateDescriptorHeap(desc,iid,output);
    if(FAILED(hr)||!IsEqualIID(iid,__uuidof(ID3D12DescriptorHeap)))return hr;
    auto &a=runtime_descriptor_arena;
    if(!a.enabled.load(std::memory_order_acquire))return hr;
    AcquireSRWLockExclusive(&a.lock);
    if(a.heap&&device==a.device&&desc->Type==D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV&&(desc->Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)) {
        if(desc->NumDescriptors>a.capacity-a.used){ReleaseSRWLockExclusive(&a.lock);static_cast<ID3D12DescriptorHeap*>(*output)->Release();*output=nullptr;return E_OUTOFMEMORY;}
        a.offsets[static_cast<ID3D12DescriptorHeap*>(*output)]=a.used;
        a.used+=desc->NumDescriptors;
    }
    ReleaseSRWLockExclusive(&a.lock);
    return hr;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE runtime_cpu_handle(ID3D12DescriptorHeap *heap) {
    auto &a=runtime_descriptor_arena;
    if(!a.enabled.load(std::memory_order_acquire))return heap->GetCPUDescriptorHandleForHeapStart();
    AcquireSRWLockShared(&a.lock);
    const auto it=a.offsets.find(heap);
    auto handle=it==a.offsets.end()?heap->GetCPUDescriptorHandleForHeapStart():a.heap->GetCPUDescriptorHandleForHeapStart();
    if(it!=a.offsets.end())handle.ptr+=SIZE_T(it->second)*a.stride;
    ReleaseSRWLockShared(&a.lock);return handle;
}
inline D3D12_GPU_DESCRIPTOR_HANDLE runtime_gpu_handle(ID3D12DescriptorHeap *heap) {
    auto &a=runtime_descriptor_arena;
    if(!a.enabled.load(std::memory_order_acquire))return heap->GetGPUDescriptorHandleForHeapStart();
    AcquireSRWLockShared(&a.lock);
    const auto it=a.offsets.find(heap);
    auto handle=it==a.offsets.end()?heap->GetGPUDescriptorHandleForHeapStart():a.heap->GetGPUDescriptorHandleForHeapStart();
    if(it!=a.offsets.end())handle.ptr+=UINT64(it->second)*a.stride;
    ReleaseSRWLockShared(&a.lock);return handle;
}
inline void runtime_set_descriptor_heaps(ID3D12GraphicsCommandList *commands,UINT count,ID3D12DescriptorHeap *const *heaps) {
    if(count>2){commands->SetDescriptorHeaps(count,heaps);return;}
    ID3D12DescriptorHeap *actual[2]{};
    auto &a=runtime_descriptor_arena;
    if(!a.enabled.load(std::memory_order_acquire)){commands->SetDescriptorHeaps(count,heaps);return;}
    AcquireSRWLockShared(&a.lock);
    for(UINT i=0;i<count;i++)actual[i]=a.offsets.count(heaps[i])?a.heap:heaps[i];
    ReleaseSRWLockShared(&a.lock);
    commands->SetDescriptorHeaps(count,actual);
}
inline UINT runtime_descriptor_arena_used() {
    auto &a=runtime_descriptor_arena;AcquireSRWLockShared(&a.lock);UINT n=a.used;ReleaseSRWLockShared(&a.lock);return n;
}
