#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

void fail(const char *operation, HRESULT result) {
    std::fprintf(stderr, "%s failed: HRESULT=0x%08lx\n", operation, result);
    ExitProcess(1);
}

void check(const char *operation, HRESULT result) {
    if (FAILED(result)) {
        fail(operation, result);
    }
}

std::vector<uint8_t> read_file(const wchar_t *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::fwprintf(stderr, L"cannot open arena: %ls\n", path);
        ExitProcess(1);
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(data.data()), size)) {
        std::fwprintf(stderr, L"cannot read arena: %ls\n", path);
        ExitProcess(1);
    }
    return data;
}

uint64_t fnv1a64(const uint8_t *data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t index = 0; index < size; ++index) {
        hash = (hash ^ data[index]) * 1099511628211ull;
    }
    return hash;
}

D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC buffer_desc(uint64_t size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::fwprintf(stderr, L"usage: %ls <weights-fp16.arena>\n", argv[0]);
        return 2;
    }

    const std::vector<uint8_t> source = read_file(argv[1]);
    if (source.empty() || source.size() % 512 != 0) {
        std::fprintf(stderr, "arena must be non-empty and 512-byte aligned\n");
        return 2;
    }

    IDXGIFactory6 *factory = nullptr;
    check("CreateDXGIFactory2", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

    IDXGIAdapter1 *adapter = nullptr;
    DXGI_ADAPTER_DESC1 adapter_desc{};
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1 *candidate = nullptr;
        if (factory->EnumAdapterByGpuPreference(
                index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            std::wcsstr(desc.Description, L"AMD") != nullptr) {
            adapter = candidate;
            adapter_desc = desc;
            break;
        }
        candidate->Release();
    }
    if (adapter == nullptr) {
        std::fprintf(stderr, "no hardware AMD DXGI adapter found\n");
        return 1;
    }

    ID3D12Device *device = nullptr;
    check("D3D12CreateDevice", D3D12CreateDevice(
        adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
    std::wprintf(
        L"adapter: %ls\ndedicated_video_memory: %llu\n",
        adapter_desc.Description,
        static_cast<unsigned long long>(adapter_desc.DedicatedVideoMemory));

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr;
    check("CreateCommandQueue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));

    ID3D12CommandAllocator *allocator = nullptr;
    check("CreateCommandAllocator", device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ID3D12GraphicsCommandList *commands = nullptr;
    check("CreateCommandList", device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&commands)));

    const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const auto readback_heap = heap_properties(D3D12_HEAP_TYPE_READBACK);
    const auto resource_desc = buffer_desc(source.size());

    ID3D12Resource *gpu_arena = nullptr;
    ID3D12Resource *upload = nullptr;
    ID3D12Resource *readback = nullptr;
    check("CreateCommittedResource(default)", device->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gpu_arena)));
    check("CreateCommittedResource(upload)", device->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));
    check("CreateCommittedResource(readback)", device->CreateCommittedResource(
        &readback_heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

    void *mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    check("Map(upload)", upload->Map(0, &no_read, &mapped));
    std::memcpy(mapped, source.data(), source.size());
    upload->Unmap(0, nullptr);

    commands->CopyBufferRegion(gpu_arena, 0, upload, 0, source.size());
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = gpu_arena;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &barrier);
    commands->CopyBufferRegion(readback, 0, gpu_arena, 0, source.size());
    check("Close", commands->Close());

    ID3D12CommandList *lists[] = {commands};
    queue->ExecuteCommandLists(1, lists);
    ID3D12Fence *fence = nullptr;
    check("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    check("Signal", queue->Signal(fence, 1));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        fail("CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
    }
    if (fence->GetCompletedValue() < 1) {
        check("SetEventOnCompletion", fence->SetEventOnCompletion(1, event));
        WaitForSingleObject(event, INFINITE);
    }
    CloseHandle(event);

    const D3D12_RANGE all_bytes{0, source.size()};
    check("Map(readback)", readback->Map(0, &all_bytes, &mapped));
    const auto *returned = static_cast<const uint8_t *>(mapped);
    const bool equal = std::memcmp(source.data(), returned, source.size()) == 0;
    const uint64_t source_hash = fnv1a64(source.data(), source.size());
    const uint64_t returned_hash = fnv1a64(returned, source.size());
    readback->Unmap(0, &no_read);

    std::printf(
        "arena_size: %zu\nsource_fnv1a64: %016llx\nreadback_fnv1a64: %016llx\nbyte_exact: %s\n",
        source.size(),
        static_cast<unsigned long long>(source_hash),
        static_cast<unsigned long long>(returned_hash),
        equal ? "yes" : "no");

    fence->Release();
    readback->Release();
    upload->Release();
    gpu_arena->Release();
    commands->Release();
    allocator->Release();
    queue->Release();
    device->Release();
    adapter->Release();
    factory->Release();
    return equal ? 0 : 1;
}
