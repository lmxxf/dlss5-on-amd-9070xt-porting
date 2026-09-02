#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "reshade.hpp"
#include "MinHook.h"

extern "C" __declspec(dllexport) const char *NAME = "DLSSNR D3D12 Weight Readback";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Reads the runtime-packed DLSSNR weight arena from its native D3D12 buffer.";

namespace {
constexpr uint64_t kArenaBytes = 147719680;
constexpr uint64_t kMinimumBytes = 1ull * 1024 * 1024;
constexpr uint64_t kMaximumBytes = 512ull * 1024 * 1024;
constexpr uint64_t kActivationBytes = 2ull * 1024 * 1024;
constexpr wchar_t kOraclePath[] = LR"(D:\DLSSNR-Lab\logs\layer-oracle.txt)";
constexpr wchar_t kArenaPath[] = LR"(D:\DLSSNR-Lab\logs\runtime-weight-arena.bin)";
constexpr wchar_t kInputPath[] = LR"(D:\DLSSNR-Lab\logs\block1-input-d3d12.bin)";
constexpr wchar_t kOutputPath[] = LR"(D:\DLSSNR-Lab\logs\block1-output-d3d12.bin)";
constexpr wchar_t kLogPath[] = LR"(D:\DLSSNR-Lab\logs\d3d12-weight-readback.txt)";

struct Candidate {
    ID3D12Resource *resource = nullptr;
    uint64_t gpu_va = 0;
    uint64_t size = 0;
};

HMODULE g_module = nullptr;
ID3D12Device *g_device = nullptr;
ID3D12CommandQueue *g_queue = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Candidate> g_candidates;
std::atomic<bool> g_done{false};
using CreateCommandQueueFn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, const D3D12_COMMAND_QUEUE_DESC *, REFIID, void **);
using CreateCommittedResourceFn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS,
    const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES,
    const D3D12_CLEAR_VALUE *, REFIID, void **);
using CreatePlacedResourceFn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, ID3D12Heap *, UINT64, const D3D12_RESOURCE_DESC *,
    D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE *, REFIID, void **);
using CreateReservedResourceFn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES,
    const D3D12_CLEAR_VALUE *, REFIID, void **);
using CreateCommittedResource1Fn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS,
    const D3D12_RESOURCE_DESC *, D3D12_RESOURCE_STATES,
    const D3D12_CLEAR_VALUE *, void *, REFIID, void **);
struct ResourceDesc1Prefix {
    uint32_t dimension;
    uint32_t padding;
    uint64_t alignment;
    uint64_t width;
};
using CreateCommittedResource2Fn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS,
    const ResourceDesc1Prefix *, uint32_t, const D3D12_CLEAR_VALUE *, void *,
    uint32_t, const DXGI_FORMAT *, REFIID, void **);
using CreatePlacedResource1Fn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, ID3D12Heap *, uint64_t, const ResourceDesc1Prefix *,
    uint32_t, const D3D12_CLEAR_VALUE *, uint32_t, const DXGI_FORMAT *,
    REFIID, void **);
CreateCommandQueueFn g_original_create_queue = nullptr;
CreateCommittedResourceFn g_original_create_resource = nullptr;
CreatePlacedResourceFn g_original_create_placed_resource = nullptr;
CreateReservedResourceFn g_original_create_reserved_resource = nullptr;
CreateCommittedResource1Fn g_original_create_resource1 = nullptr;
CreateCommittedResource2Fn g_original_create_resource2 = nullptr;
CreatePlacedResource1Fn g_original_create_placed1 = nullptr;
CreateCommittedResource2Fn g_original_create_resource3 = nullptr;
CreatePlacedResource1Fn g_original_create_placed2 = nullptr;

void append_log(const char *format, ...) {
    AcquireSRWLockExclusive(&g_lock);
    if (FILE *file = _wfopen(kLogPath, L"ab")) {
        va_list args;
        va_start(args, format);
        std::vfprintf(file, format, args);
        va_end(args);
        std::fclose(file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

HRESULT STDMETHODCALLTYPE hook_create_queue(
    ID3D12Device *self, const D3D12_COMMAND_QUEUE_DESC *desc,
    REFIID iid, void **output) {
    const HRESULT hr = g_original_create_queue(self, desc, iid, output);
    if (SUCCEEDED(hr) && output != nullptr && *output != nullptr &&
        desc != nullptr && desc->Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        g_queue = static_cast<ID3D12CommandQueue *>(*output);
        append_log("direct_queue=%p\n", g_queue);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_resource(
    ID3D12Device *self, const D3D12_HEAP_PROPERTIES *heap_properties,
    D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC *desc,
    D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *clear_value,
    REFIID iid, void **output) {
    const HRESULT hr = g_original_create_resource(
        self, heap_properties, heap_flags, desc, initial_state,
        clear_value, iid, output);
    if (FAILED(hr) || output == nullptr || *output == nullptr || desc == nullptr ||
        desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        desc->Width < kMinimumBytes || desc->Width > kMaximumBytes) {
        return hr;
    }
    auto *native = static_cast<ID3D12Resource *>(*output);
    const uint64_t gpu_va = native->GetGPUVirtualAddress();
    if (native == nullptr || gpu_va == 0) {
        return hr;
    }
    native->AddRef();
    AcquireSRWLockExclusive(&g_lock);
    g_candidates.push_back({native, gpu_va, desc->Width});
    ReleaseSRWLockExclusive(&g_lock);
    append_log("candidate resource=%p gpu_va=0x%llx size=%llu initial_state=0x%x heap=%u\n", native,
        static_cast<unsigned long long>(gpu_va),
        static_cast<unsigned long long>(desc->Width),
        static_cast<unsigned>(initial_state),
        heap_properties == nullptr ? 0u : static_cast<unsigned>(heap_properties->Type));
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_placed_resource(
    ID3D12Device *self, ID3D12Heap *heap, UINT64 heap_offset,
    const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE *clear_value, REFIID iid, void **output) {
    const HRESULT hr = g_original_create_placed_resource(
        self, heap, heap_offset, desc, initial_state, clear_value, iid, output);
    if (FAILED(hr) || output == nullptr || *output == nullptr || desc == nullptr ||
        desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        desc->Width < kMinimumBytes || desc->Width > kMaximumBytes) {
        return hr;
    }
    auto *native = static_cast<ID3D12Resource *>(*output);
    const uint64_t gpu_va = native->GetGPUVirtualAddress();
    if (gpu_va == 0) return hr;
    native->AddRef();
    AcquireSRWLockExclusive(&g_lock);
    g_candidates.push_back({native, gpu_va, desc->Width});
    ReleaseSRWLockExclusive(&g_lock);
    append_log("placed resource=%p gpu_va=0x%llx size=%llu heap_offset=%llu initial_state=0x%x\n",
        native, static_cast<unsigned long long>(gpu_va),
        static_cast<unsigned long long>(desc->Width),
        static_cast<unsigned long long>(heap_offset), static_cast<unsigned>(initial_state));
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_reserved_resource(
    ID3D12Device *self, const D3D12_RESOURCE_DESC *desc,
    D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *clear_value,
    REFIID iid, void **output) {
    const HRESULT hr = g_original_create_reserved_resource(
        self, desc, initial_state, clear_value, iid, output);
    if (FAILED(hr) || output == nullptr || *output == nullptr || desc == nullptr ||
        desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        desc->Width < kMinimumBytes || desc->Width > kMaximumBytes) return hr;
    auto *native = static_cast<ID3D12Resource *>(*output);
    const uint64_t gpu_va = native->GetGPUVirtualAddress();
    if (gpu_va == 0) return hr;
    native->AddRef();
    AcquireSRWLockExclusive(&g_lock);
    g_candidates.push_back({native, gpu_va, desc->Width});
    ReleaseSRWLockExclusive(&g_lock);
    append_log("reserved resource=%p gpu_va=0x%llx size=%llu initial_state=0x%x\n",
        native, static_cast<unsigned long long>(gpu_va),
        static_cast<unsigned long long>(desc->Width), static_cast<unsigned>(initial_state));
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_resource1(
    ID3D12Device *self, const D3D12_HEAP_PROPERTIES *heap_properties,
    D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC *desc,
    D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *clear_value,
    void *protected_session, REFIID iid, void **output) {
    const HRESULT hr = g_original_create_resource1(
        self, heap_properties, heap_flags, desc, initial_state, clear_value,
        protected_session, iid, output);
    if (FAILED(hr) || output == nullptr || *output == nullptr || desc == nullptr ||
        desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        desc->Width < kMinimumBytes || desc->Width > kMaximumBytes) return hr;
    auto *native = static_cast<ID3D12Resource *>(*output);
    const uint64_t gpu_va = native->GetGPUVirtualAddress();
    if (gpu_va == 0) return hr;
    native->AddRef();
    AcquireSRWLockExclusive(&g_lock);
    g_candidates.push_back({native, gpu_va, desc->Width});
    ReleaseSRWLockExclusive(&g_lock);
    append_log("committed1 resource=%p gpu_va=0x%llx size=%llu initial_state=0x%x heap=%u\n",
        native, static_cast<unsigned long long>(gpu_va),
        static_cast<unsigned long long>(desc->Width), static_cast<unsigned>(initial_state),
        heap_properties == nullptr ? 0u : static_cast<unsigned>(heap_properties->Type));
    return hr;
}

void track_extended_resource(
    const char *label, const ResourceDesc1Prefix *desc, uint32_t initial,
    void **output) {
    if (output == nullptr || *output == nullptr || desc == nullptr ||
        desc->dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        desc->width < kMinimumBytes || desc->width > kMaximumBytes) return;
    auto *native = static_cast<ID3D12Resource *>(*output);
    const uint64_t gpu_va = native->GetGPUVirtualAddress();
    if (gpu_va == 0) return;
    native->AddRef();
    AcquireSRWLockExclusive(&g_lock);
    g_candidates.push_back({native, gpu_va, desc->width});
    ReleaseSRWLockExclusive(&g_lock);
    append_log("%s resource=%p gpu_va=0x%llx size=%llu initial=0x%x\n",
        label, native, static_cast<unsigned long long>(gpu_va),
        static_cast<unsigned long long>(desc->width), initial);
}

HRESULT STDMETHODCALLTYPE hook_create_resource2(
    ID3D12Device *self, const D3D12_HEAP_PROPERTIES *heap_properties,
    D3D12_HEAP_FLAGS heap_flags, const ResourceDesc1Prefix *desc,
    uint32_t initial, const D3D12_CLEAR_VALUE *clear_value, void *protected_session,
    uint32_t format_count, const DXGI_FORMAT *formats, REFIID iid, void **output) {
    const HRESULT hr = g_original_create_resource2(
        self, heap_properties, heap_flags, desc, initial, clear_value,
        protected_session, format_count, formats, iid, output);
    if (SUCCEEDED(hr)) track_extended_resource("committed2", desc, initial, output);
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_placed1(
    ID3D12Device *self, ID3D12Heap *heap, uint64_t offset,
    const ResourceDesc1Prefix *desc, uint32_t initial,
    const D3D12_CLEAR_VALUE *clear_value, uint32_t format_count,
    const DXGI_FORMAT *formats, REFIID iid, void **output) {
    const HRESULT hr = g_original_create_placed1(
        self, heap, offset, desc, initial, clear_value,
        format_count, formats, iid, output);
    if (SUCCEEDED(hr)) track_extended_resource("placed1", desc, initial, output);
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_resource3(
    ID3D12Device *self, const D3D12_HEAP_PROPERTIES *heap_properties,
    D3D12_HEAP_FLAGS heap_flags, const ResourceDesc1Prefix *desc,
    uint32_t initial, const D3D12_CLEAR_VALUE *clear_value, void *protected_session,
    uint32_t format_count, const DXGI_FORMAT *formats, REFIID iid, void **output) {
    const HRESULT hr = g_original_create_resource3(
        self, heap_properties, heap_flags, desc, initial, clear_value,
        protected_session, format_count, formats, iid, output);
    if (SUCCEEDED(hr)) track_extended_resource("committed3", desc, initial, output);
    return hr;
}

HRESULT STDMETHODCALLTYPE hook_create_placed2(
    ID3D12Device *self, ID3D12Heap *heap, uint64_t offset,
    const ResourceDesc1Prefix *desc, uint32_t initial,
    const D3D12_CLEAR_VALUE *clear_value, uint32_t format_count,
    const DXGI_FORMAT *formats, REFIID iid, void **output) {
    const HRESULT hr = g_original_create_placed2(
        self, heap, offset, desc, initial, clear_value,
        format_count, formats, iid, output);
    if (SUCCEEDED(hr)) track_extended_resource("placed2", desc, initial, output);
    return hr;
}

void on_init_device(reshade::api::device *device) {
    if (device->get_api() != reshade::api::device_api::d3d12) return;
    g_device = reinterpret_cast<ID3D12Device *>(device->get_native());
    append_log("device=%p\n", g_device);
    void **vtable = *reinterpret_cast<void ***>(g_device);
    for (unsigned index = 44; index <= 80; ++index) {
        append_log("device_vtable[%u]=%p\n", index, vtable[index]);
    }
    if (MH_Initialize() != MH_OK ||
        MH_CreateHook(vtable[8], reinterpret_cast<void *>(&hook_create_queue),
            reinterpret_cast<void **>(&g_original_create_queue)) != MH_OK ||
        MH_CreateHook(vtable[27], reinterpret_cast<void *>(&hook_create_resource),
            reinterpret_cast<void **>(&g_original_create_resource)) != MH_OK ||
        MH_CreateHook(vtable[29], reinterpret_cast<void *>(&hook_create_placed_resource),
            reinterpret_cast<void **>(&g_original_create_placed_resource)) != MH_OK ||
        MH_CreateHook(vtable[30], reinterpret_cast<void *>(&hook_create_reserved_resource),
            reinterpret_cast<void **>(&g_original_create_reserved_resource)) != MH_OK ||
        MH_EnableHook(vtable[8]) != MH_OK || MH_EnableHook(vtable[27]) != MH_OK ||
        MH_EnableHook(vtable[29]) != MH_OK || MH_EnableHook(vtable[30]) != MH_OK) {
        append_log("native_hook_failed\n");
    } else {
        append_log("native_hooks_enabled create_queue=%p committed=%p placed=%p reserved=%p\n",
            vtable[8], vtable[27], vtable[29], vtable[30]);
        const MH_STATUS resource1_create = MH_CreateHook(
            vtable[53], reinterpret_cast<void *>(&hook_create_resource1),
            reinterpret_cast<void **>(&g_original_create_resource1));
        const MH_STATUS resource1_enable = resource1_create == MH_OK
            ? MH_EnableHook(vtable[53]) : resource1_create;
        append_log("committed1_hook create=%d enable=%d target=%p\n",
            resource1_create, resource1_enable, vtable[53]);
        struct ExtendedHook { unsigned index; void *hook; void **original; } hooks[] = {
            {69, reinterpret_cast<void *>(&hook_create_resource2), reinterpret_cast<void **>(&g_original_create_resource2)},
            {70, reinterpret_cast<void *>(&hook_create_placed1), reinterpret_cast<void **>(&g_original_create_placed1)},
            {76, reinterpret_cast<void *>(&hook_create_resource3), reinterpret_cast<void **>(&g_original_create_resource3)},
            {77, reinterpret_cast<void *>(&hook_create_placed2), reinterpret_cast<void **>(&g_original_create_placed2)},
        };
        for (const ExtendedHook &hook : hooks) {
            const MH_STATUS create = MH_CreateHook(vtable[hook.index], hook.hook, hook.original);
            const MH_STATUS enable = create == MH_OK ? MH_EnableHook(vtable[hook.index]) : create;
            append_log("extended_hook index=%u create=%d enable=%d target=%p\n",
                hook.index, create, enable, vtable[hook.index]);
        }
    }
}

struct OracleAddresses { uint64_t arena = 0, input = 0, output = 0; };

OracleAddresses read_oracle_addresses() {
    FILE *file = _wfopen(kOraclePath, L"rb");
    if (file == nullptr) return {};
    char line[256]{};
    OracleAddresses addresses{};
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        unsigned long long parsed = 0;
        if (std::sscanf(line, "calculated_arena_base=0x%llx", &parsed) == 1) {
            addresses.arena = parsed;
        } else if (std::sscanf(line, "input=0x%llx", &parsed) == 1) {
            addresses.input = parsed;
        } else if (std::sscanf(line, "output=0x%llx", &parsed) == 1) {
            addresses.output = parsed;
        }
    }
    std::fclose(file);
    return addresses;
}

HRESULT wait_queue(ID3D12CommandQueue *queue, ID3D12Device *device) {
    ID3D12Fence *fence = nullptr;
    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) return hr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) { fence->Release(); return HRESULT_FROM_WIN32(GetLastError()); }
    hr = queue->Signal(fence, 1);
    if (SUCCEEDED(hr)) hr = fence->SetEventOnCompletion(1, event);
    if (SUCCEEDED(hr) && WaitForSingleObject(event, 30000) != WAIT_OBJECT_0) hr = E_FAIL;
    CloseHandle(event);
    fence->Release();
    return hr;
}

HRESULT readback(
    Candidate candidate, uint64_t gpu_va, uint64_t bytes, const wchar_t *path) {
    const uint64_t offset = gpu_va - candidate.gpu_va;
    if (offset + bytes > candidate.size) return E_INVALIDARG;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *readback_buffer = nullptr;
    HRESULT hr = g_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&readback_buffer));
    if (FAILED(hr)) return hr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    hr = g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(hr)) hr = g_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
    if (SUCCEEDED(hr)) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = candidate.resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);
        list->CopyBufferRegion(readback_buffer, 0, candidate.resource, offset, bytes);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        hr = list->Close();
    }
    if (SUCCEEDED(hr)) {
        ID3D12CommandList *lists[] = {list};
        g_queue->ExecuteCommandLists(1, lists);
        hr = wait_queue(g_queue, g_device);
    }
    if (SUCCEEDED(hr)) {
        void *mapped = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
        hr = readback_buffer->Map(0, &range, &mapped);
        if (SUCCEEDED(hr)) {
            if (FILE *file = _wfopen(path, L"wb")) {
                if (std::fwrite(mapped, 1, static_cast<size_t>(bytes), file) != bytes)
                    hr = E_FAIL;
                std::fclose(file);
            } else hr = E_FAIL;
            D3D12_RANGE written{0, 0};
            readback_buffer->Unmap(0, &written);
        }
    }
    if (list) list->Release();
    if (allocator) allocator->Release();
    readback_buffer->Release();
    return hr;
}

DWORD WINAPI worker(void *) {
    for (unsigned attempt = 0; attempt < 600 && !g_done.load(); ++attempt) {
        const OracleAddresses addresses = read_oracle_addresses();
        if (addresses.arena != 0 && addresses.input != 0 && addresses.output != 0 &&
            g_device != nullptr && g_queue != nullptr) {
            Candidate arena_match{}, input_match{}, output_match{};
            AcquireSRWLockShared(&g_lock);
            for (const Candidate &candidate : g_candidates) {
                if (addresses.arena >= candidate.gpu_va && addresses.arena + kArenaBytes <= candidate.gpu_va + candidate.size)
                    arena_match = candidate;
                if (addresses.input >= candidate.gpu_va && addresses.input + kActivationBytes <= candidate.gpu_va + candidate.size)
                    input_match = candidate;
                if (addresses.output >= candidate.gpu_va && addresses.output + kActivationBytes <= candidate.gpu_va + candidate.size)
                    output_match = candidate;
            }
            ReleaseSRWLockShared(&g_lock);
            if (arena_match.resource && input_match.resource && output_match.resource) {
                HRESULT hr = wait_queue(g_queue, g_device);
                if (SUCCEEDED(hr)) hr = readback(arena_match, addresses.arena, kArenaBytes, kArenaPath);
                if (SUCCEEDED(hr)) hr = readback(input_match, addresses.input, kActivationBytes, kInputPath);
                if (SUCCEEDED(hr)) hr = readback(output_match, addresses.output, kActivationBytes, kOutputPath);
                append_log("readback arena=0x%llx input=0x%llx output=0x%llx hr=0x%08lx\n",
                    static_cast<unsigned long long>(addresses.arena),
                    static_cast<unsigned long long>(addresses.input),
                    static_cast<unsigned long long>(addresses.output),
                    static_cast<unsigned long>(hr));
                if (SUCCEEDED(hr)) g_done.store(true);
                return 0;
            }
        }
        Sleep(100);
    }
    append_log("worker_timeout\n");
    return 1;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&worker), &pinned)) {
            return FALSE;
        }
        DeleteFileW(kLogPath);
        DeleteFileW(kArenaPath);
        DeleteFileW(kInputPath);
        DeleteFileW(kOutputPath);
        if (!reshade::register_addon(instance)) return FALSE;
        reshade::register_event<reshade::addon_event::init_device>(on_init_device);
        if (HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
        reshade::unregister_addon(instance);
    }
    return TRUE;
}
