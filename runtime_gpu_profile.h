#pragma once
// One diagnostic frame. Only timestamps are read back, after the game queue fence.
struct RuntimeGpuProfile {
    ID3D12QueryHeap *queries{};
    ID3D12Resource *readback{};
    ID3D12Fence *fence{};
    std::atomic<unsigned> state{0};
    UINT64 frequency{};
    void Create(ID3D12Device *device) {
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=42;
        dmlrt_check("profile queries",device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)));
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=42*sizeof(UINT64);d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        dmlrt_check("profile readback",device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        dmlrt_check("profile fence",device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    }
    bool Begin(ID3D12GraphicsCommandList *cmd,UINT64 frames) {
        if(!queries||frames<60)return false;
        unsigned expected=0;if(!state.compare_exchange_strong(expected,1))return false;
        cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,0);return true;
    }
    void Mark(ID3D12GraphicsCommandList *cmd,UINT index){cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,index);}
    void End(ID3D12GraphicsCommandList *cmd){
        cmd->ResolveQueryData(queries,D3D12_QUERY_TYPE_TIMESTAMP,0,42,readback,0);state.store(2);
    }
};
