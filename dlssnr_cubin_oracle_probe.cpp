#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <reshade.hpp>
#include "MinHook.h"
#include "capture_raw_buffer_cubin.inc"

extern "C" __declspec(dllexport) const char *NAME = "DLSSNR CUBIN Oracle Probe";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Uses the live NvAPI CUBIN backend to copy block1 output into a probe-owned buffer.";

namespace {
constexpr uintptr_t kForward1HRva = 0x637b0;
constexpr uintptr_t kCreateBackendRva = 0x447b0;
constexpr uintptr_t kSetContextRva = 0x44b70;
constexpr uintptr_t kSetCubinRva = 0x44b60;
constexpr uintptr_t kGetKernelRva = 0x44830;
constexpr uintptr_t kLaunchRva = 0x449a0;
constexpr UINT64 kPerCaptureBytes = 64ull * 1024 * 1024;
constexpr UINT64 kCaptureBytes = 3 * kPerCaptureBytes;
constexpr wchar_t kLogPath[] = LR"(D:\DLSSNR-Lab\logs\cubin-oracle.txt)";
constexpr wchar_t kOutputPath[] = LR"(D:\DLSSNR-Lab\logs\block1-output-raw.bin)";

using Forward1H = void (*)(void *, void *, void *, void *, int, int);
using CreateBackend = void *(*)(void *);
using SetContext = void (*)(void *, void *);
using SetCubin = void (*)(void *, void *, uint32_t);
using GetKernel = void *(*)(void *, const char *, uint32_t, uint32_t, uint32_t, uint64_t);
using ContextSync = int (*)(void *, void *, uint32_t, uint32_t);
using BindKernel = int (*)(void *, void *);
using DispatchKernel = int (*)(
    void *, void *, void *, uint32_t, uint32_t, uint32_t);
using BackendLaunch = int64_t (*)(
    void *, void *, uint32_t, uint32_t, uint32_t, void *, uint64_t, uint8_t);

struct CopyParams {
    UINT64 source;
    UINT64 destination;
    uint32_t uint4_count;
    uint32_t padding;
};

HMODULE g_runtime = nullptr;
Forward1H g_original = nullptr;
ID3D12Device *g_device = nullptr;
ID3D12CommandQueue *g_queue = nullptr;
ID3D12Resource *g_destination = nullptr;
std::atomic<unsigned> g_calls{0};
std::atomic<bool> g_pending{false};
std::atomic<bool> g_execute_seen{false};
std::atomic<bool> g_finished{false};
std::atomic<bool> g_launch_armed{false};
std::atomic<bool> g_launch_captured{false};
BackendLaunch g_original_backend_launch = nullptr;
void *g_live_ngx_context = nullptr;
void *g_live_command_context = nullptr;
UINT64 g_live_output = 0;
UINT64 g_live_input = 0;
UINT64 g_live_optional2 = 0;
uint8_t g_live_params[0x100]{};
uint64_t g_live_param_bytes = 0;

void log_line(const char *text, long value = 0) {
    FILE *file = _wfopen(kLogPath, L"ab");
    if (file != nullptr) {
        std::fprintf(file, "%s=0x%08lx\n", text, value);
        std::fclose(file);
    }
}

using CreateCommandQueueFn = HRESULT (STDMETHODCALLTYPE *)(
    ID3D12Device *, const D3D12_COMMAND_QUEUE_DESC *, REFIID, void **);
CreateCommandQueueFn g_original_create_queue = nullptr;

HRESULT STDMETHODCALLTYPE hook_create_queue(
    ID3D12Device *self, const D3D12_COMMAND_QUEUE_DESC *desc,
    REFIID iid, void **output) {
    const HRESULT result = g_original_create_queue(self, desc, iid, output);
    if (SUCCEEDED(result) && output != nullptr && *output != nullptr &&
        desc != nullptr && desc->Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        g_queue = static_cast<ID3D12CommandQueue *>(*output);
    }
    return result;
}

void on_init_device(reshade::api::device *device) {
    if (g_device != nullptr || device->get_api() != reshade::api::device_api::d3d12) return;
    g_device = reinterpret_cast<ID3D12Device *>(device->get_native());
    g_device->AddRef();
    void **vtable = *reinterpret_cast<void ***>(g_device);
    const MH_STATUS initialized = MH_Initialize();
    if ((initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED) &&
        MH_CreateHook(vtable[8], reinterpret_cast<void *>(&hook_create_queue),
            reinterpret_cast<void **>(&g_original_create_queue)) == MH_OK) {
        MH_EnableHook(vtable[8]);
    }
}

DWORD WINAPI readback_worker(void *parameter);

int64_t hook_backend_launch(
    void *self, void *kernel, uint32_t gx, uint32_t gy, uint32_t gz,
    void *wrapper, uint64_t bytes, uint8_t flag) {
    if (g_launch_armed.load() && !g_launch_captured.exchange(true) && wrapper != nullptr) {
        std::memcpy(
            &g_live_ngx_context, static_cast<const uint8_t *>(self) + 0x08,
            sizeof(g_live_ngx_context));
        std::memcpy(
            &g_live_command_context, static_cast<const uint8_t *>(self) + 0x10,
            sizeof(g_live_command_context));
        void *blob = nullptr;
        std::memcpy(&blob, wrapper, sizeof(blob));
        if (blob != nullptr && bytes >= 16) {
            g_live_param_bytes = bytes < sizeof(g_live_params) ? bytes : sizeof(g_live_params);
            std::memcpy(g_live_params, blob, static_cast<size_t>(g_live_param_bytes));
            std::memcpy(&g_live_input, blob, sizeof(g_live_input));
            std::memcpy(&g_live_output, static_cast<const uint8_t *>(blob) + 0x08,
                sizeof(g_live_output));
            if (bytes >= 0x40) {
                std::memcpy(&g_live_optional2, static_cast<const uint8_t *>(blob) + 0x38,
                    sizeof(g_live_optional2));
            }
        }
    }
    return g_original_backend_launch(self, kernel, gx, gy, gz, wrapper, bytes, flag);
}

bool create_destination() {
    if (g_device == nullptr || g_destination != nullptr) return g_destination != nullptr;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kCaptureBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const HRESULT result = g_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&g_destination));
    log_line("CreateCommittedResource", result);
    return SUCCEEDED(result);
}

void hook_forward(
    void *self, void *inputs, void *outputs, void *context, int width, int height) {
    const unsigned call = g_calls.fetch_add(1);
    if (call == 0) g_launch_armed.store(true);
    g_original(self, inputs, outputs, context, width, height);
    if (call == 0) g_launch_armed.store(false);
    if (call != 0 || !create_destination()) return;

    constexpr UINT64 kResourceAlignment = 2ull * 1024 * 1024;
    const UINT64 input_prefix = g_live_input & (kResourceAlignment - 1);
    const UINT64 output_prefix = g_live_output & (kResourceAlignment - 1);
    const UINT64 input_source = g_live_input - input_prefix;
    const UINT64 output_source = g_live_output - output_prefix;
    void *ngx_context = g_live_ngx_context;
    void *command_context = g_live_command_context;
    if (input_source == 0 || output_source == 0 || g_live_optional2 == 0 ||
        ngx_context == nullptr || command_context == nullptr) return;
    if (FILE *file = _wfopen(kLogPath, L"ab")) {
        std::fprintf(file, "live_param_bytes=%llu\nlive_params_qwords=",
            static_cast<unsigned long long>(g_live_param_bytes));
        for (uint64_t offset = 0; offset + 8 <= g_live_param_bytes; offset += 8) {
            unsigned long long value = 0;
            std::memcpy(&value, g_live_params + offset, sizeof(value));
            std::fprintf(file, "%s0x%llx", offset == 0 ? "" : ",", value);
        }
        std::fprintf(file, "\n");
        std::fclose(file);
    }

    auto create_backend = reinterpret_cast<CreateBackend>(
        reinterpret_cast<uintptr_t>(g_runtime) + kCreateBackendRva);
    auto set_context = reinterpret_cast<SetContext>(
        reinterpret_cast<uintptr_t>(g_runtime) + kSetContextRva);
    auto set_cubin = reinterpret_cast<SetCubin>(
        reinterpret_cast<uintptr_t>(g_runtime) + kSetCubinRva);
    auto get_kernel = reinterpret_cast<GetKernel>(
        reinterpret_cast<uintptr_t>(g_runtime) + kGetKernelRva);

    log_line("before_create_backend");
    void *backend = create_backend(ngx_context);
    log_line("after_create_backend", backend == nullptr ? 0 : 1);
    if (backend == nullptr) return;
    log_line("before_set_context");
    set_context(backend, command_context);
    log_line("after_set_context");
    log_line("before_set_cubin");
    set_cubin(
        backend, _tmp_capture_raw_buffer_cubin,
        _tmp_capture_raw_buffer_cubin_len);
    log_line("after_set_cubin");
    log_line("before_get_kernel");
    void *kernel = get_kernel(backend, "capture_raw_buffer", 256, 1, 1, 0);
    log_line("after_get_kernel", kernel == nullptr ? 0 : 1);
    if (kernel == nullptr) return;

    CopyParams input_params{
        input_source,
        g_destination->GetGPUVirtualAddress(),
        static_cast<uint32_t>(kPerCaptureBytes / 16),
        0,
    };
    CopyParams output_params{
        output_source,
        g_destination->GetGPUVirtualAddress() + kPerCaptureBytes,
        static_cast<uint32_t>(kPerCaptureBytes / 16),
        0,
    };
    CopyParams optional2_params{
        g_live_optional2,
        g_destination->GetGPUVirtualAddress() + 2 * kPerCaptureBytes,
        static_cast<uint32_t>(kPerCaptureBytes / 16),
        0,
    };
    void **context_vtable = *reinterpret_cast<void ***>(ngx_context);
    auto sync = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
    auto bind = reinterpret_cast<BindKernel>(context_vtable[0xd8 / 8]);
    auto dispatch = reinterpret_cast<DispatchKernel>(context_vtable[0x140 / 8]);
    struct ParameterDescriptor {
        void *vtable;
        void *blob;
        uint32_t bytes;
        uint32_t padding;
    } descriptor{
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_runtime) + 0xb28b8),
        &input_params,
        sizeof(input_params),
        0,
    };
    log_line("before_sync");
    const int sync_result = sync(ngx_context, command_context, 0, 0);
    log_line("after_sync", sync_result);
    const int bind_result = sync_result == 0 ? bind(ngx_context, kernel) : -1;
    log_line("after_bind", bind_result);
    const int dispatch_result = bind_result == 0
        ? dispatch(
            ngx_context, &descriptor, command_context,
            (input_params.uint4_count + 255) / 256, 1, 1)
        : -1;
    log_line("after_input_dispatch", dispatch_result);
    if (dispatch_result != 0) return;
    descriptor.blob = &output_params;
    const int output_dispatch_result = dispatch(
        ngx_context, &descriptor, command_context,
        (output_params.uint4_count + 255) / 256, 1, 1);
    log_line("after_output_dispatch", output_dispatch_result);
    if (output_dispatch_result != 0) return;
    descriptor.blob = &optional2_params;
    const int optional2_dispatch_result = dispatch(
        ngx_context, &descriptor, command_context,
        (optional2_params.uint4_count + 255) / 256, 1, 1);
    log_line("after_optional2_dispatch", optional2_dispatch_result);
    if (optional2_dispatch_result != 0) return;

    FILE *file = _wfopen(kLogPath, L"ab");
    if (file != nullptr) {
        std::fprintf(
            file,
            "width=%d\nheight=%d\ninput_source=0x%llx\noutput_source=0x%llx\n"
            "input_prefix=0x%llx\noutput_prefix=0x%llx\n"
            "optional2_source=0x%llx\ndestination=0x%llx\n"
            "backend=%p\nkernel=%p\nuint4_count=%u\n",
            width, height,
            static_cast<unsigned long long>(input_source),
            static_cast<unsigned long long>(output_source),
            static_cast<unsigned long long>(input_prefix),
            static_cast<unsigned long long>(output_prefix),
            static_cast<unsigned long long>(g_live_optional2),
            static_cast<unsigned long long>(g_destination->GetGPUVirtualAddress()),
            backend, kernel, input_params.uint4_count);
        std::fclose(file);
    }
    g_pending.store(true);
    if (g_queue != nullptr && !g_finished.exchange(true)) {
        g_queue->AddRef();
        HANDLE thread = CreateThread(nullptr, 0, readback_worker, g_queue, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    }
}

bool wait_queue(ID3D12CommandQueue *queue, ID3D12Fence *fence, UINT64 value) {
    if (FAILED(queue->Signal(fence, value))) return false;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) return false;
    const HRESULT result = fence->SetEventOnCompletion(value, event);
    if (SUCCEEDED(result)) WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    return SUCCEEDED(result);
}

DWORD WINAPI readback_worker(void *parameter) {
    auto *queue = static_cast<ID3D12CommandQueue *>(parameter);
    Sleep(100);
    ID3D12Fence *fence = nullptr;
    HRESULT result = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(result) || !wait_queue(queue, fence, 1)) {
        log_line("wait_prior", result);
        queue->Release();
        return 1;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kCaptureBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *readback = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *commands = nullptr;
    result = g_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&readback));
    if (SUCCEEDED(result)) result = g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (SUCCEEDED(result)) result = g_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
        IID_PPV_ARGS(&commands));
    if (SUCCEEDED(result)) {
        commands->CopyBufferRegion(readback, 0, g_destination, 0, kCaptureBytes);
        result = commands->Close();
    }
    if (SUCCEEDED(result)) {
        ID3D12CommandList *lists[] = {commands};
        queue->ExecuteCommandLists(1, lists);
        if (!wait_queue(queue, fence, 2)) result = E_FAIL;
    }
    if (SUCCEEDED(result)) {
        void *mapped = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(kCaptureBytes)};
        result = readback->Map(0, &range, &mapped);
        if (SUCCEEDED(result)) {
            FILE *file = _wfopen(kOutputPath, L"wb");
            if (file != nullptr) {
                std::fwrite(mapped, 1, kCaptureBytes, file);
                std::fclose(file);
            }
            const D3D12_RANGE none{0, 0};
            readback->Unmap(0, &none);
        }
    }
    log_line("readback", result);
    if (commands != nullptr) commands->Release();
    if (allocator != nullptr) allocator->Release();
    if (readback != nullptr) readback->Release();
    if (fence != nullptr) fence->Release();
    queue->Release();
    return SUCCEEDED(result) ? 0 : 1;
}

DWORD WINAPI hook_worker(void *) {
    for (unsigned attempt = 0; attempt < 600 && g_runtime == nullptr; ++attempt) {
        g_runtime = GetModuleHandleW(L"nvngx_dlssnr.dll");
        if (g_runtime == nullptr) Sleep(100);
    }
    if (g_runtime == nullptr) return 1;
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return 1;
    void *target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(g_runtime) + kForward1HRva);
    void *backend_target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(g_runtime) + kLaunchRva);
    if (MH_CreateHook(
            backend_target, reinterpret_cast<void *>(&hook_backend_launch),
            reinterpret_cast<void **>(&g_original_backend_launch)) != MH_OK) return 2;
    if (MH_CreateHook(
            target, reinterpret_cast<void *>(&hook_forward),
            reinterpret_cast<void **>(&g_original)) != MH_OK) return 3;
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK ? 0 : 4;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&hook_worker), &pinned)) return FALSE;
        if (!reshade::register_addon(instance)) return FALSE;
        reshade::register_event<reshade::addon_event::init_device>(on_init_device);
        HANDLE thread = CreateThread(nullptr, 0, &hook_worker, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
        reshade::unregister_addon(instance);
    }
    return TRUE;
}
