#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, HRESULT result) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: 0x%08lx\n", name, result);
        ExitProcess(1);
    }
}
static std::vector<unsigned char> read_file(const wchar_t *path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) ExitProcess(2);
    size_t size = static_cast<size_t>(stream.tellg());
    stream.seekg(0);
    std::vector<unsigned char> bytes(size);
    stream.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}
static D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES result{};
    result.Type = type;
    result.CreationNodeMask = result.VisibleNodeMask = 1;
    return result;
}
static D3D12_RESOURCE_DESC buffer(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC result{};
    result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    result.Width = bytes;
    result.Height = 1;
    result.DepthOrArraySize = result.MipLevels = 1;
    result.SampleDesc.Count = 1;
    result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    result.Flags = flags;
    return result;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 8) {
        std::fwprintf(stderr,
            L"usage: %ls matrix.f32 feature.f32 source.f32 oracle.f32 "
            L"output.f32 width height\n", argv[0]);
        return 2;
    }
    auto matrix = read_file(argv[1]);
    auto feature = read_file(argv[2]);
    auto source = read_file(argv[3]);
    auto oracle = read_file(argv[4]);
    const UINT width = _wtoi(argv[6]), height = _wtoi(argv[7]);
    const UINT64 pixels = static_cast<UINT64>(width) * height;
    if (!pixels || matrix.size() != 32 * 3 * 4 ||
        feature.size() != pixels * 32 * 4 || source.size() != pixels * 4 * 4 ||
        oracle.size() != pixels * 4 * 4) return 2;

    IDXGIFactory6 *factory = nullptr;
    check("factory", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    IDXGIAdapter1 *adapter = nullptr;
    DXGI_ADAPTER_DESC1 adapter_desc{};
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1 *candidate = nullptr;
        if (factory->EnumAdapterByGpuPreference(index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            wcsstr(desc.Description, L"AMD")) {
            adapter = candidate;
            adapter_desc = desc;
            break;
        }
        candidate->Release();
    }
    if (!adapter) return 1;
    ID3D12Device *device = nullptr;
    check("device", D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                      IID_PPV_ARGS(&device)));
    std::wprintf(L"adapter: %ls\n", adapter_desc.Description);

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    ID3D12CommandQueue *queue = nullptr;
    check("queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
    ID3D12CommandAllocator *allocator = nullptr;
    check("allocator", device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ID3D12GraphicsCommandList *commands = nullptr;
    check("commands", device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
        IID_PPV_ARGS(&commands)));

    const char shader[] = R"(
StructuredBuffer<float> weight:register(t0);
StructuredBuffer<float> feature:register(t1);
StructuredBuffer<float> source:register(t2);
RWStructuredBuffer<float> output:register(u0);
uint linear_id(uint3 g,uint3 t){return (g.y*65535+g.x)*64+t.x;}
[numthreads(64,1,1)] void main(uint3 g:SV_GroupID,uint3 t:SV_GroupThreadID) {
    uint pixel=linear_id(g,t);if(pixel>=PIXELS)return;
    [unroll]for(uint color=0;color<3;color++) {
        float residual=0;
        [unroll]for(uint channel=0;channel<32;channel++)
            residual+=feature[pixel*32+channel]*weight[channel*3+color];
        output[pixel*4+color]=saturate(source[pixel*4+color]+residual);
    }
    output[pixel*4+3]=1.0;
})";
    ID3DBlob *code = nullptr, *error = nullptr;
    char pixel_count[32];
    std::snprintf(pixel_count, sizeof(pixel_count), "%llu",
                  static_cast<unsigned long long>(pixels));
    D3D_SHADER_MACRO macros[] = {{"PIXELS", pixel_count}, {nullptr, nullptr}};
    check("compile", D3DCompile(shader, sizeof(shader) - 1, nullptr, macros,
        nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &code, &error));
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
    ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 3};
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = {2, ranges};
    D3D12_ROOT_SIGNATURE_DESC signature_desc{};
    signature_desc.NumParameters = 1;
    signature_desc.pParameters = &parameter;
    ID3DBlob *signature_blob = nullptr;
    check("serialize", D3D12SerializeRootSignature(&signature_desc,
        D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error));
    ID3D12RootSignature *signature = nullptr;
    check("signature", device->CreateRootSignature(0,
        signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(),
        IID_PPV_ARGS(&signature)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = signature;
    pipeline_desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ID3D12PipelineState *pipeline = nullptr;
    check("pipeline", device->CreateComputePipelineState(
        &pipeline_desc, IID_PPV_ARGS(&pipeline)));

    auto upload_heap = heap(D3D12_HEAP_TYPE_UPLOAD);
    auto default_heap = heap(D3D12_HEAP_TYPE_DEFAULT);
    auto readback_heap = heap(D3D12_HEAP_TYPE_READBACK);
    ID3D12Resource *inputs[3]{};
    std::vector<unsigned char> *bytes[3] = {&matrix, &feature, &source};
    for (int index = 0; index < 3; ++index) {
        auto desc = buffer(bytes[index]->size());
        check("input", device->CreateCommittedResource(&upload_heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&inputs[index])));
        void *mapped = nullptr;
        D3D12_RANGE empty{0, 0};
        inputs[index]->Map(0, &empty, &mapped);
        std::memcpy(mapped, bytes[index]->data(), bytes[index]->size());
        inputs[index]->Unmap(0, nullptr);
    }
    const UINT64 output_bytes = pixels * 4 * 4;
    auto output_desc = buffer(output_bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback_desc = buffer(output_bytes);
    ID3D12Resource *output = nullptr, *readback = nullptr;
    check("output", device->CreateCommittedResource(&default_heap,
        D3D12_HEAP_FLAG_NONE, &output_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr, IID_PPV_ARGS(&output)));
    check("readback", device->CreateCommittedResource(&readback_heap,
        D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&readback)));

    D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc{
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ID3D12DescriptorHeap *descriptors = nullptr;
    check("descriptors", device->CreateDescriptorHeap(
        &descriptor_desc, IID_PPV_ARGS(&descriptors)));
    UINT stride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
    for (int index = 0; index < 3; ++index) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Buffer.StructureByteStride = 4;
        view.Buffer.NumElements = static_cast<UINT>(bytes[index]->size() / 4);
        device->CreateShaderResourceView(inputs[index], &view, handle);
        handle.ptr += stride;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC output_view{};
    output_view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    output_view.Buffer.StructureByteStride = 4;
    output_view.Buffer.NumElements = output_bytes / 4;
    device->CreateUnorderedAccessView(output, nullptr, &output_view, handle);

    ID3D12DescriptorHeap *heaps[] = {descriptors};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(signature);
    commands->SetComputeRootDescriptorTable(
        0, descriptors->GetGPUDescriptorHandleForHeapStart());
    commands->SetPipelineState(pipeline);
    const UINT64 groups=(pixels+63)/64;
    commands->Dispatch(static_cast<UINT>(std::min<UINT64>(groups,65535)),
                       static_cast<UINT>((groups+65534)/65535),1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    commands->CopyBufferRegion(readback, 0, output, 0, output_bytes);
    check("close", commands->Close());

    LARGE_INTEGER frequency, begin, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    ID3D12CommandList *lists[] = {commands};
    queue->ExecuteCommandLists(1, lists);
    ID3D12Fence *fence = nullptr;
    check("fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence)));
    queue->Signal(fence, 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    QueryPerformanceCounter(&end);

    void *mapped = nullptr;
    D3D12_RANGE all{0, output_bytes};
    readback->Map(0, &all, &mapped);
    const float *got = static_cast<const float *>(mapped);
    const float *want = reinterpret_cast<const float *>(oracle.data());
    double absolute = 0, square = 0, dot = 0, got_square = 0, want_square = 0;
    float maximum = 0;
    for (size_t index = 0; index < output_bytes / 4; ++index) {
        double delta = got[index] - want[index];
        absolute += std::abs(delta);
        square += delta * delta;
        maximum = std::max(maximum, static_cast<float>(std::abs(delta)));
        dot += got[index] * want[index];
        got_square += got[index] * got[index];
        want_square += want[index] * want[index];
    }
    std::ofstream stream(argv[5], std::ios::binary);
    stream.write(reinterpret_cast<const char *>(got), output_bytes);
    const double count = output_bytes / 4;
    std::printf("submit_to_fence_ms=%.3f MAE=%.9g RMSE=%.9g max=%.9g cosine=%.12g\n",
        1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart,
        absolute / count, std::sqrt(square / count), maximum,
        dot / std::sqrt(got_square * want_square));
    return 0;
}
