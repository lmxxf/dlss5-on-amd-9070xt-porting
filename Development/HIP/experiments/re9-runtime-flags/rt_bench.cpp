// Standalone LmxxfNrRuntime.dll driver (RE9 host style: ABI 2, full FrameInfo with exposure).
// usage: rt_bench.exe <LmxxfNrRuntime.dll> <modules_dir> <WxH[,WxH...]> <frames_per_size> <rounds>
// Per round and size: mean wall ms of the last 3/4 frames, FNV-1a of the last frame's private output,
// process local VRAM (DXGI) and the runtime status line. Input: fixed pseudo-random RGBA16F (same for all runs).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "LmxxfNrApi.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void Die(const char *what, long hr = 0) { std::fprintf(stderr, "FAIL: %s %08lx\n", what, hr); std::exit(1); }
static void Check(HRESULT hr, const char *what) { if (FAILED(hr)) Die(what, hr); }
static uint16_t Half(float f)
{
    uint32_t b; std::memcpy(&b, &f, 4);
    uint32_t s = (b >> 16) & 0x8000, e = (b >> 23) & 0xff, m = b & 0x7fffff;
    if (e < 113) return uint16_t(s);
    return uint16_t(s | ((e - 112) << 10) | (m >> 13));
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 6) { std::fprintf(stderr, "usage: rt_bench <dll> <modules> <WxH,...> <frames> <rounds>\n"); return 2; }
    setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) Die("LoadLibrary");
    auto getApi = reinterpret_cast<int32_t (*)(uint32_t, LmxxfNrApi *)>(GetProcAddress(dll, "LmxxfNrGetApi"));
    LmxxfNrApi api{}; api.struct_size = sizeof(api);
    if (!getApi || getApi(2, &api) != LMXXF_NR_OK) Die("GetApi(2)");
    std::vector<std::pair<UINT, UINT>> sizes;
    for (wchar_t *p = argv[3]; *p;) { UINT w = wcstoul(p, &p, 10); if (*p == L'x') ++p; UINT h = wcstoul(p, &p, 10); sizes.push_back({w, h}); if (*p == L',') ++p; }
    const int frames = _wtoi(argv[4]), rounds = _wtoi(argv[5]);

    IDXGIFactory4 *factory = nullptr; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    IDXGIAdapter1 *adapter = nullptr; ID3D12Device *device = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
        if (d.VendorId == 0x1002 && SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) break;
        adapter->Release(); adapter = nullptr;
    }
    if (!device) Die("AMD device");
    IDXGIAdapter3 *adapter3 = nullptr; adapter->QueryInterface(IID_PPV_ARGS(&adapter3));
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr; Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "queue");
    ID3D12CommandAllocator *allocA = nullptr, *allocB = nullptr;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocA)), "allocA");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocB)), "allocB");
    ID3D12GraphicsCommandList *listA = nullptr, *listB = nullptr;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocA, nullptr, IID_PPV_ARGS(&listA)), "listA");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocB, nullptr, IID_PPV_ARGS(&listB)), "listB");
    listA->Close(); listB->Close();
    ID3D12Fence *fence = nullptr; Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); UINT64 fv = 0;
    auto wait = [&] { Check(queue->Signal(fence, ++fv), "signal"); Check(fence->SetEventOnCompletion(fv, ev), "evt"); if (WaitForSingleObject(ev, 30000) != WAIT_OBJECT_0) Die("timeout"); };
    auto exec = [&](ID3D12GraphicsCommandList *l) { ID3D12CommandList *a[] = {l}; queue->ExecuteCommandLists(1, a); };

    D3D12_HEAP_PROPERTIES def{}; def.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_HEAP_PROPERTIES rb{}; rb.Type = D3D12_HEAP_TYPE_READBACK;
    auto buffer = [&](D3D12_HEAP_PROPERTIES &hp, UINT64 bytes, D3D12_RESOURCE_STATES st) {
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1; d.DepthOrArraySize = d.MipLevels = 1;
        d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; ID3D12Resource *r = nullptr;
        Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, nullptr, IID_PPV_ARGS(&r)), "buffer"); return r; };

    // exposure 1x1 R32 = 1
    D3D12_RESOURCE_DESC ed{}; ed.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; ed.Width = ed.Height = 1; ed.DepthOrArraySize = ed.MipLevels = 1;
    ed.Format = DXGI_FORMAT_R32_FLOAT; ed.SampleDesc.Count = 1;
    ID3D12Resource *exposure = nullptr; Check(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &ed, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&exposure)), "exposure");
    {
        ID3D12Resource *eu = buffer(up, 256, D3D12_RESOURCE_STATE_GENERIC_READ); void *m = nullptr; D3D12_RANGE z{0, 0}; eu->Map(0, &z, &m); *(float *)m = 1.f; eu->Unmap(0, nullptr);
        allocA->Reset(); listA->Reset(allocA, nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{}; dst.pResource = exposure; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = eu; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, 1, 1, 1, 256};
        listA->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER b{}; b.Transition = {exposure, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        listA->ResourceBarrier(1, &b); listA->Close(); exec(listA); wait(); eu->Release();
    }

    std::wstring modules = argv[2];
    LmxxfNrCreateInfo ci{}; ci.struct_size = sizeof(ci); ci.device = device; ci.queue = queue; ci.assets_directory = modules.c_str();
    void *ctx = nullptr;
    if (api.Create(&ci, &ctx) != LMXXF_NR_OK) { char e[512]{}; api.GetLastError(e, sizeof e); std::fprintf(stderr, "Create: %s\n", e); return 1; }
    if (api.PrepareSession(ctx) != LMXXF_NR_OK) { char e[512]{}; api.GetLastError(e, sizeof e); std::fprintf(stderr, "PrepareSession: %s\n", e); return 1; }

    for (int r = 0; r < rounds; ++r)
        for (auto [W, H] : sizes)
        {
            D3D12_RESOURCE_DESC td{}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = W; td.Height = H; td.DepthOrArraySize = td.MipLevels = 1;
            td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1;
            ID3D12Resource *color = nullptr; Check(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&color)), "color");
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 total = 0; device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
            ID3D12Resource *upc = buffer(up, total, D3D12_RESOURCE_STATE_GENERIC_READ);
            { void *m = nullptr; D3D12_RANGE z{0, 0}; upc->Map(0, &z, &m); uint32_t s = 0x9e3779b9u;
              for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) { uint16_t *p = (uint16_t *)((char *)m + y * fp.Footprint.RowPitch + 8 * x);
                  for (int c = 0; c < 3; ++c) { s = s * 1664525u + 1013904223u; p[c] = Half(float((s >> 8) & 0xffff) / 65535.f * 0.9f); } p[3] = 0x3c00; }
              upc->Unmap(0, nullptr); }
            allocA->Reset(); listA->Reset(allocA, nullptr);
            { D3D12_TEXTURE_COPY_LOCATION dst{}, src{}; dst.pResource = color; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.pResource = upc; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
              listA->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
              D3D12_RESOURCE_BARRIER b{}; b.Transition = {color, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}; listA->ResourceBarrier(1, &b); }
            listA->Close(); exec(listA); wait();

            LmxxfNrFrameInfo fi{}; fi.struct_size = sizeof(fi); fi.color_width = W; fi.color_height = H; fi.color = color;
            fi.color_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; fi.exposure = exposure;
            fi.exposure_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; fi.pre_exposure = fi.exposure_scale = 1.f;
            double sum = 0; int used = 0; uint64_t hash = 0; ID3D12Resource *out = nullptr;
            for (int f = 0; f < frames; ++f)
            {
                auto t0 = std::chrono::steady_clock::now();
                fi.frame_id = uint64_t(r) * 100000 + f;
                allocA->Reset(); listA->Reset(allocA, nullptr); allocB->Reset(); listB->Reset(allocB, nullptr);
                LmxxfNrJob job{}; job.struct_size = sizeof(job);
                if (api.PrepareFrame(ctx, &fi, &job) != LMXXF_NR_OK) { char e[512]{}; api.GetLastError(e, sizeof e); std::fprintf(stderr, "PrepareFrame %ux%u: %s\n", W, H, e); return 1; }
                if (api.RecordInputs(ctx, job.handle, listA) != LMXXF_NR_OK) Die("RecordInputs");
                listA->Close();
                if (api.RecordOutputs(ctx, job.handle, listB) != LMXXF_NR_OK) { char e[512]{}; api.GetLastError(e, sizeof e); std::fprintf(stderr, "RecordOutputs: %s\n", e); return 1; }
                listB->Close();
                exec(listA);
                if (reinterpret_cast<int32_t (*)(void *, void *)>(api.EnqueueHip)(ctx, job.handle) != LMXXF_NR_OK) { char e[512]{}; api.GetLastError(e, sizeof e); std::fprintf(stderr, "EnqueueHip: %s\n", e); return 1; }
                exec(listB);
                if (api.Retire(ctx, job.handle) != LMXXF_NR_OK) Die("Retire");
                wait();
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (f >= frames / 4) { sum += ms; ++used; }
                out = static_cast<ID3D12Resource *>(job.private_output);
            }
            if (out)
            {
                D3D12_RESOURCE_DESC od = out->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT ofp{}; UINT64 ob = 0; UINT rows = 0; UINT64 rowBytes = 0;
                device->GetCopyableFootprints(&od, 0, 1, 0, &ofp, &rows, &rowBytes, &ob);
                ID3D12Resource *rbk = buffer(rb, ob, D3D12_RESOURCE_STATE_COPY_DEST);
                allocA->Reset(); listA->Reset(allocA, nullptr);
                D3D12_RESOURCE_BARRIER b{}; b.Transition = {out, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
                listA->ResourceBarrier(1, &b);
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{}; dst.pResource = rbk; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = ofp;
                src.pResource = out; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; listA->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                std::swap(b.Transition.StateBefore, b.Transition.StateAfter); listA->ResourceBarrier(1, &b);
                listA->Close(); exec(listA); wait();
                void *m = nullptr; rbk->Map(0, nullptr, &m); hash = 1469598103934665603ull;
                for (UINT y = 0; y < rows; ++y) { const uint8_t *p = (const uint8_t *)m + y * ofp.Footprint.RowPitch; for (UINT64 i = 0; i < rowBytes; ++i) { hash ^= p[i]; hash *= 1099511628211ull; } }
                rbk->Unmap(0, nullptr); rbk->Release();
            }
            DXGI_QUERY_VIDEO_MEMORY_INFO vm{}; if (adapter3) adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm);
            char status[640]{}; api.GetStatus(ctx, status, sizeof status);
            std::printf("round=%d size=%ux%u mean_ms=%.3f hash=%016llx vram_mib=%llu status=%s\n", r, W, H, used ? sum / used : 0.0,
                        (unsigned long long)hash, (unsigned long long)(vm.CurrentUsage >> 20), status);
            upc->Release(); color->Release();
        }
    api.Drain(ctx);
    api.Destroy(ctx);
    std::printf("rt_bench: ok\n");
    return 0;
}
