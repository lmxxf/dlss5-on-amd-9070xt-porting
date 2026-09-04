#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <d3d12.h>
#define DML_TARGET_VERSION_USE_LATEST
#ifndef _Maybenull_
#define _Maybenull_
#endif
#include <DirectML.h>
#include "reshade.hpp"
struct DmlFailure { const char *operation; HRESULT result; };
#define DMLRT_FAILURE(name, result) throw DmlFailure{name, result}
#include "directml_gemm_runtime.h"

extern "C" __declspec(dllexport) const char *NAME = "DLSS5 AMD Resident Lifecycle Probe";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Initializes a persistent DirectML operator on the game's D3D12 device.";

namespace {
constexpr wchar_t kLog[] = LR"(D:\DLSSNR-Lab\logs\resident-lifecycle-probe.txt)";
static const GUID kDmlDevice = {0x6dbd6437, 0x96fd, 0x423f,
    {0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f}};
static const GUID kDmlRecorder = {0xe6857a76, 0x2e3e, 0x4fdd,
    {0xbf, 0xf4, 0x5d, 0x2b, 0xa1, 0x0f, 0xb4, 0x53}};
using CreateDml = HRESULT(WINAPI *)(ID3D12Device *, DML_CREATE_DEVICE_FLAGS,
                                    REFIID, void **);

SRWLOCK g_log_lock = SRWLOCK_INIT;
std::atomic<bool> g_started{false}, g_ready{false}, g_failed{false};
std::atomic<unsigned long long> g_presents{0};
ID3D12Device *g_device = nullptr;
IDMLDevice *g_dml = nullptr;
IDMLCommandRecorder *g_recorder = nullptr;
ID3D12CommandQueue *g_queue = nullptr;
ID3D12CommandAllocator *g_allocator = nullptr;
ID3D12GraphicsCommandList *g_list = nullptr;
ID3D12Fence *g_fence = nullptr;
HANDLE g_event = nullptr;
DmlGemmOperator *g_operator = nullptr;
ID3D12Resource *g_probe_input = nullptr, *g_probe_weight = nullptr,
                 *g_probe_output = nullptr;
double g_probe_gpu_ms = 0.0;

void log(const char *format, ...) {
    AcquireSRWLockExclusive(&g_log_lock);
    if (FILE *file = _wfopen(kLog, L"ab")) {
        va_list args;
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fclose(file);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

ID3D12Resource *make_buffer(UINT64 bytes, D3D12_HEAP_TYPE heap_type,
                            D3D12_RESOURCE_STATES state,
                            D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heap_type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ID3D12Resource *resource = nullptr;
    dmlrt_check("probe CreateCommittedResource",
        g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          state, nullptr,
                                          IID_PPV_ARGS(&resource)));
    return resource;
}

void run_warm_probe() {
    constexpr UINT iterations = 100;
    const UINT64 input_bytes = g_operator->ABytes();
    const UINT64 weight_bytes = g_operator->BBytes();
    const UINT64 output_bytes = g_operator->OutputBytes();
    g_probe_input = make_buffer(input_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_probe_weight = make_buffer(weight_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_probe_output = make_buffer(output_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *zero = make_buffer(input_bytes, D3D12_HEAP_TYPE_UPLOAD,
                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                       D3D12_RESOURCE_FLAG_NONE);
    void *mapped = nullptr;
    D3D12_RANGE none{0, 0};
    dmlrt_check("probe Map", zero->Map(0, &none, &mapped));
    std::memset(mapped, 0, static_cast<size_t>(input_bytes));
    zero->Unmap(0, nullptr);

    dmlrt_check("probe allocator reset", g_allocator->Reset());
    dmlrt_check("probe list reset", g_list->Reset(g_allocator, nullptr));
    g_list->CopyBufferRegion(g_probe_input, 0, zero, 0, input_bytes);
    g_list->CopyBufferRegion(g_probe_weight, 0, zero, 0, weight_bytes);
    D3D12_RESOURCE_BARRIER transitions[2]{};
    for (UINT i = 0; i < 2; ++i) {
        transitions[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transitions[i].Transition.pResource = i ? g_probe_weight : g_probe_input;
        transitions[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transitions[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        transitions[i].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    g_list->ResourceBarrier(2, transitions);
    g_operator->Bind(g_probe_input, g_probe_weight, g_probe_output);

    D3D12_QUERY_HEAP_DESC query_desc{};
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query_desc.Count = 2;
    ID3D12QueryHeap *queries = nullptr;
    dmlrt_check("probe query heap",
                g_device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&queries)));
    ID3D12Resource *readback = make_buffer(16, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    g_list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    for (UINT i = 0; i < iterations; ++i) {
        g_operator->Record(g_recorder, g_list);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = g_probe_output;
        g_list->ResourceBarrier(1, &barrier);
    }
    g_list->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    g_list->ResolveQueryData(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                             readback, 0);
    dmlrt_check("probe close", g_list->Close());
    ID3D12CommandList *lists[] = {g_list};
    g_queue->ExecuteCommandLists(1, lists);
    dmlrt_check("probe signal", g_queue->Signal(g_fence, 2));
    dmlrt_check("probe completion", g_fence->SetEventOnCompletion(2, g_event));
    if (WaitForSingleObject(g_event, 30000) != WAIT_OBJECT_0)
        throw DmlFailure{"probe wait", HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    UINT64 *timestamps = nullptr;
    D3D12_RANGE range{0, 16};
    dmlrt_check("probe timestamp map", readback->Map(0, &range,
                                                     reinterpret_cast<void **>(&timestamps)));
    UINT64 frequency = 0;
    dmlrt_check("probe timestamp frequency", g_queue->GetTimestampFrequency(&frequency));
    g_probe_gpu_ms = 1000.0 * double(timestamps[1] - timestamps[0]) /
                     double(frequency) / iterations;
    readback->Unmap(0, nullptr);
    queries->Release();
    readback->Release();
    zero->Release();
}

DWORD WINAPI initialize_worker(void *) {
    const ULONGLONG begin = GetTickCount64();
    HMODULE library = LoadLibraryW(L"DirectML.dll");
    auto create = library ? reinterpret_cast<CreateDml>(
                                GetProcAddress(library, "DMLCreateDevice"))
                          : nullptr;
    HRESULT hr = create ? create(g_device, DML_CREATE_DEVICE_FLAG_NONE,
                                 kDmlDevice, reinterpret_cast<void **>(&g_dml))
                        : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr))
        hr = g_dml->CreateCommandRecorder(kDmlRecorder,
                                          reinterpret_cast<void **>(&g_recorder));
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g_queue));
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_allocator));
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_allocator, nullptr,
        IID_PPV_ARGS(&g_list));
    if (SUCCEEDED(hr)) hr = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                   IID_PPV_ARGS(&g_fence));
    if (SUCCEEDED(hr)) g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(hr) && !g_event) hr = HRESULT_FROM_WIN32(GetLastError());

    if (SUCCEEDED(hr)) {
        g_operator = new DmlGemmOperator();
        try {
            g_operator->Create(g_dml, g_device, 1, 522240, 64, 96);
            g_operator->RecordInitialization(g_recorder, g_list);
        } catch (const DmlFailure &failure) {
            hr = failure.result;
            log("resident_operator_failed operation=%s hr=0x%08x\n",
                failure.operation, static_cast<unsigned>(failure.result));
        }
    }
    if (SUCCEEDED(hr)) hr = g_list->Close();
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {g_list};
        g_queue->ExecuteCommandLists(1, lists);
        hr = g_queue->Signal(g_fence, 1);
    }
    if (SUCCEEDED(hr)) hr = g_fence->SetEventOnCompletion(1, g_event);
    if (SUCCEEDED(hr) && WaitForSingleObject(g_event, 30000) != WAIT_OBJECT_0)
        hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (SUCCEEDED(hr)) {
        try {
            run_warm_probe();
        } catch (const DmlFailure &failure) {
            hr = failure.result;
            log("resident_execution_failed operation=%s hr=0x%08x\n",
                failure.operation, static_cast<unsigned>(failure.result));
        }
    }
    const HRESULT removed = g_device->GetDeviceRemovedReason();
    if (SUCCEEDED(hr) && SUCCEEDED(removed)) {
        g_ready.store(true);
        log("resident_ready device=%p dml=%p operator=522240x64x96 gpu_ms=%.6f iterations=100 init_wall_ms=%llu removed=0x%08x\n",
            g_device, g_dml, g_probe_gpu_ms, GetTickCount64() - begin,
            static_cast<unsigned>(removed));
    } else {
        g_failed.store(true);
        log("resident_failed hr=0x%08x removed=0x%08x wall_ms=%llu\n",
            static_cast<unsigned>(hr), static_cast<unsigned>(removed),
            GetTickCount64() - begin);
    }
    return SUCCEEDED(hr) && SUCCEEDED(removed) ? 0 : 1;
}

void on_init_device(reshade::api::device *device) {
    if (device->get_api() != reshade::api::device_api::d3d12 ||
        g_started.exchange(true))
        return;
    g_device = reinterpret_cast<ID3D12Device *>(
        static_cast<uintptr_t>(device->get_native()));
    g_device->AddRef();
    log("resident_start device=%p\n", g_device);
    if (HANDLE thread = CreateThread(nullptr, 0, initialize_worker, nullptr, 0, nullptr))
        CloseHandle(thread);
    else {
        g_failed.store(true);
        log("resident_thread_failed error=%lu\n", GetLastError());
    }
}

void on_present(reshade::api::command_queue *, reshade::api::swapchain *,
                const reshade::api::rect *, const reshade::api::rect *, uint32_t,
                const reshade::api::rect *) {
    const auto n = ++g_presents;
    if (n == 1 || n % 600 == 0)
        log("present=%llu ready=%u failed=%u\n", n, g_ready.load() ? 1 : 0,
            g_failed.load() ? 1 : 0);
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        DeleteFileW(kLog);
        if (!reshade::register_addon(instance)) return FALSE;
        reshade::register_event<reshade::addon_event::init_device>(on_init_device);
        reshade::register_event<reshade::addon_event::present>(on_present);
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::present>(on_present);
        reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
        reshade::unregister_addon(instance);
    }
    return TRUE;
}
