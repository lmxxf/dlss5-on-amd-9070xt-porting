#pragma once
// Opt-in, one-frame GPU readback. Never enabled in the embedded distribution.
// Included after runtime globals/make_buffer. No shader or output modifications.
struct DisplayAudit {
    ID3D12Resource *before{},*after{},*back{},*body{},*weights{};
    ID3D12Fence *fence{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 texture_bytes{},body_bytes{},generation{};
    unsigned state{}; // 0 idle, 1 recorded, 2 fence pending
    ULONGLONG last_check{};
    static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
        D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);
    }
    void CopyTexture(ID3D12GraphicsCommandList*c,ID3D12Resource*src,ID3D12Resource*dst){
        D3D12_TEXTURE_COPY_LOCATION s{},d{};s.pResource=src;s.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        d.pResource=dst;d.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;d.PlacedFootprint=footprint;c->CopyTextureRegion(&d,0,0,0,&s,nullptr);
    }
    bool Begin(ID3D12GraphicsCommandList*c,ID3D12Resource*src,UINT64 gen){
        if(state||runtime_bundle_present()||g_output_mode!=0||gen<120)return false;
        const auto now=GetTickCount64();if(now-last_check<1000)return false;last_check=now;
        const wchar_t*request=LR"(D:\DLSSNR-Lab\audit-display.txt)";
        if(GetFileAttributesW(request)==INVALID_FILE_ATTRIBUTES)return false;
        try{
            auto desc=src->GetDesc();g_device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&texture_bytes);
            body_bytes=g_b70.body->GetDesc().Width;generation=gen;
            auto rb=[&](UINT64 n){return make_buffer(n,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_NONE);};
            before=rb(texture_bytes);after=rb(texture_bytes);back=rb(texture_bytes);body=rb(body_bytes);weights=rb(384);
            dmlrt_check("audit fence",g_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        }catch(const DmlFailure&e){log("display_audit_failed hr=%08x\n",unsigned(e.result));Release();return false;}
        DeleteFileW(request); // consume only this explicit one-shot request
        CopyTexture(c,src,before); // source is the actual backbuffer, COPY_SOURCE
        Barrier(c,g_b70.body,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        c->CopyBufferRegion(body,0,g_b70.body,0,body_bytes);
        Barrier(c,g_b70.body,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(c,g_b70.out_weight,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        c->CopyBufferRegion(weights,0,g_b70.out_weight,0,384);
        Barrier(c,g_b70.out_weight,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return true;
    }
    void End(ID3D12GraphicsCommandList*c,ID3D12Resource*display,ID3D12Resource*target){
        CopyTexture(c,display,after); // display already COPY_SOURCE
        Barrier(c,target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        CopyTexture(c,target,back);
        Barrier(c,target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        state=1;log("display_audit_recorded generation=%llu body_bytes=%llu\n",generation,body_bytes);
    }
    bool Save(ID3D12Resource*r,UINT64 n,const wchar_t*name){
        void*p=nullptr;D3D12_RANGE read{0,SIZE_T(n)};if(FAILED(r->Map(0,&read,&p)))return false;
        std::wstring path=LR"(D:\DLSSNR-Lab\logs\display-audit\)";path+=name;
        FILE*f=_wfopen(path.c_str(),L"wb");bool ok=false;if(f){ok=fwrite(p,1,SIZE_T(n),f)==n;ok=(fclose(f)==0)&&ok;}
        D3D12_RANGE none{0,0};r->Unmap(0,&none);return ok;
    }
    void Poll(ID3D12CommandQueue*q){
        // Called at the next Present: ReShade has flushed the prior immediate list.
        if(state==1){if(SUCCEEDED(q->Signal(fence,1)))state=2;return;}
        if(state!=2||fence->GetCompletedValue()!=1)return;
        CreateDirectoryW(LR"(D:\DLSSNR-Lab\logs\display-audit)",nullptr);
        bool ok=Save(before,texture_bytes,L"before.r10")&&Save(after,texture_bytes,L"after.r10")&&Save(back,texture_bytes,L"backbuffer.r10")&&Save(body,body_bytes,L"body.f32")&&Save(weights,384,L"weights.f32");
        FILE*f=_wfopen(LR"(D:\DLSSNR-Lab\logs\display-audit\metadata.json)",L"wb");
        if(f){fprintf(f,"{\"generation\":%llu,\"width\":1920,\"height\":1080,\"row_pitch\":%u,\"body_bytes\":%llu,\"mode\":0,\"fence_completed\":true,\"files_complete\":%s}\n",generation,footprint.Footprint.RowPitch,body_bytes,ok?"true":"false");fclose(f);}else ok=false;
        log("display_audit_saved generation=%llu success=%u\n",generation,ok?1u:0u);Release();
    }
    void Release(){for(auto**p:{&before,&after,&back,&body,&weights}){if(*p)(*p)->Release();*p=nullptr;}if(fence)fence->Release();fence=nullptr;state=0;}
} g_display_audit;
