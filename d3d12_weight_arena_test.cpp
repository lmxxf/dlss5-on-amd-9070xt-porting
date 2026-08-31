#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
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
    if (argc != 2 && argc != 3) {
        std::fwprintf(
            stderr, L"usage: %ls <weights-fp16.arena> [record-offsets.u32]\n", argv[0]);
        return 2;
    }

    const std::vector<uint8_t> source = read_file(argv[1]);
    const std::vector<uint8_t> offset_bytes = argc == 3
        ? read_file(argv[2])
        : std::vector<uint8_t>{};
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

    bool compute_equal = true;
    if (!offset_bytes.empty()) {
        if (offset_bytes.size() != 153 * sizeof(uint32_t)) {
            std::fprintf(stderr, "offset table must contain exactly 153 u32 values\n");
            return 2;
        }

        static const char shader_source[] = R"(
ByteAddressBuffer weights : register(t0);
StructuredBuffer<uint> offsets : register(t1);
RWStructuredBuffer<uint> results : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x < 153) {
        results[id.x] = weights.Load(offsets[id.x]);
    }
}
)";
        ID3DBlob *shader = nullptr;
        ID3DBlob *errors = nullptr;
        HRESULT compile_result = D3DCompile(
            shader_source, sizeof(shader_source) - 1, "weight-arena-readback", nullptr, nullptr,
            "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
        if (FAILED(compile_result)) {
            if (errors != nullptr) {
                std::fprintf(stderr, "%.*s\n", static_cast<int>(errors->GetBufferSize()),
                    static_cast<const char *>(errors->GetBufferPointer()));
            }
            fail("D3DCompile", compile_result);
        }
        if (errors != nullptr) {
            errors->Release();
        }

        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 2;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 2;
        D3D12_ROOT_PARAMETER root_parameter{};
        root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameter.DescriptorTable.NumDescriptorRanges = 2;
        root_parameter.DescriptorTable.pDescriptorRanges = ranges;
        root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 1;
        root_desc.pParameters = &root_parameter;

        ID3DBlob *root_blob = nullptr;
        ID3DBlob *root_errors = nullptr;
        check("D3D12SerializeRootSignature", D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &root_errors));
        if (root_errors != nullptr) {
            root_errors->Release();
        }
        ID3D12RootSignature *root_signature = nullptr;
        check("CreateRootSignature", device->CreateRootSignature(
            0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_signature)));
        root_blob->Release();

        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = root_signature;
        pipeline_desc.CS.pShaderBytecode = shader->GetBufferPointer();
        pipeline_desc.CS.BytecodeLength = shader->GetBufferSize();
        ID3D12PipelineState *pipeline = nullptr;
        check("CreateComputePipelineState", device->CreateComputePipelineState(
            &pipeline_desc, IID_PPV_ARGS(&pipeline)));
        shader->Release();

        const auto offset_desc = buffer_desc(offset_bytes.size());
        ID3D12Resource *offset_buffer = nullptr;
        check("CreateCommittedResource(offsets)", device->CreateCommittedResource(
            &upload_heap, D3D12_HEAP_FLAG_NONE, &offset_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&offset_buffer)));
        check("Map(offsets)", offset_buffer->Map(0, &no_read, &mapped));
        std::memcpy(mapped, offset_bytes.data(), offset_bytes.size());
        offset_buffer->Unmap(0, nullptr);

        const uint64_t result_size = offset_bytes.size();
        auto result_desc = buffer_desc(result_size);
        result_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource *result_buffer = nullptr;
        ID3D12Resource *result_readback = nullptr;
        check("CreateCommittedResource(results)", device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &result_desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&result_buffer)));
        const auto result_readback_desc = buffer_desc(result_size);
        check("CreateCommittedResource(result readback)", device->CreateCommittedResource(
            &readback_heap, D3D12_HEAP_FLAG_NONE, &result_readback_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result_readback)));

        D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{};
        descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptor_heap_desc.NumDescriptors = 3;
        descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ID3D12DescriptorHeap *descriptor_heap = nullptr;
        check("CreateDescriptorHeap", device->CreateDescriptorHeap(
            &descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap)));
        const UINT descriptor_size = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor =
            descriptor_heap->GetCPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC weights_srv{};
        weights_srv.Format = DXGI_FORMAT_R32_TYPELESS;
        weights_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        weights_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        weights_srv.Buffer.NumElements = static_cast<UINT>(source.size() / 4);
        weights_srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(gpu_arena, &weights_srv, descriptor);
        descriptor.ptr += descriptor_size;

        D3D12_SHADER_RESOURCE_VIEW_DESC offsets_srv{};
        offsets_srv.Format = DXGI_FORMAT_UNKNOWN;
        offsets_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        offsets_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        offsets_srv.Buffer.NumElements = 153;
        offsets_srv.Buffer.StructureByteStride = sizeof(uint32_t);
        device->CreateShaderResourceView(offset_buffer, &offsets_srv, descriptor);
        descriptor.ptr += descriptor_size;

        D3D12_UNORDERED_ACCESS_VIEW_DESC results_uav{};
        results_uav.Format = DXGI_FORMAT_UNKNOWN;
        results_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        results_uav.Buffer.NumElements = 153;
        results_uav.Buffer.StructureByteStride = sizeof(uint32_t);
        device->CreateUnorderedAccessView(result_buffer, nullptr, &results_uav, descriptor);

        check("Reset allocator", allocator->Reset());
        check("Reset command list", commands->Reset(allocator, pipeline));
        D3D12_RESOURCE_BARRIER arena_barrier{};
        arena_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        arena_barrier.Transition.pResource = gpu_arena;
        arena_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        arena_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        arena_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &arena_barrier);
        ID3D12DescriptorHeap *heaps[] = {descriptor_heap};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetComputeRootSignature(root_signature);
        commands->SetComputeRootDescriptorTable(
            0, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
        commands->Dispatch(3, 1, 1);
        D3D12_RESOURCE_BARRIER result_barriers[2]{};
        result_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        result_barriers[0].UAV.pResource = result_buffer;
        result_barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        result_barriers[1].Transition.pResource = result_buffer;
        result_barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        result_barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        result_barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(2, result_barriers);
        commands->CopyBufferRegion(result_readback, 0, result_buffer, 0, result_size);
        check("Close compute commands", commands->Close());
        queue->ExecuteCommandLists(1, lists);
        check("Signal compute", queue->Signal(fence, 2));
        HANDLE compute_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        check("SetEventOnCompletion compute", fence->SetEventOnCompletion(2, compute_event));
        WaitForSingleObject(compute_event, INFINITE);
        CloseHandle(compute_event);

        const D3D12_RANGE result_bytes{0, static_cast<SIZE_T>(result_size)};
        check("Map(result readback)", result_readback->Map(0, &result_bytes, &mapped));
        const auto *gpu_values = static_cast<const uint32_t *>(mapped);
        const auto *offsets = reinterpret_cast<const uint32_t *>(offset_bytes.data());
        for (size_t index = 0; index < 153; ++index) {
            uint32_t expected = 0;
            std::memcpy(&expected, source.data() + offsets[index], sizeof(expected));
            if (gpu_values[index] != expected) {
                std::fprintf(stderr,
                    "compute mismatch at record %zu: offset=%u expected=%08x actual=%08x\n",
                    index, offsets[index], expected, gpu_values[index]);
                compute_equal = false;
                break;
            }
        }
        result_readback->Unmap(0, &no_read);
        std::printf("compute_record_reads: %s (153/153)\n", compute_equal ? "yes" : "no");

        descriptor_heap->Release();
        result_readback->Release();
        result_buffer->Release();
        offset_buffer->Release();
        pipeline->Release();
        root_signature->Release();
    }

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
    return equal && compute_equal ? 0 : 1;
}
