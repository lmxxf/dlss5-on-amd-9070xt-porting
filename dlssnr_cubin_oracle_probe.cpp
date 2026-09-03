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
constexpr UINT64 kCaptureBytes = 600ull << 20;
constexpr UINT64 kProbeBytes = 10 * kPerCaptureBytes;
constexpr wchar_t kLogPath[] = LR"(D:\DLSSNR-Lab\logs\cubin-oracle.txt)";
constexpr wchar_t kOutputPath[] = LR"(D:\DLSSNR-Lab\logs\block1-output-raw.bin)";
constexpr wchar_t kTracePath[] = LR"(D:\DLSSNR-Lab\logs\backend-launch-trace.txt)";
constexpr bool kCaptureBlock48Identity = false;
constexpr bool kCaptureBlock66 = false;
constexpr bool kCaptureBlock62 = false;
constexpr bool kCaptureBlock56 = false;
constexpr bool kCaptureBlock48 = false;
constexpr bool kCompareBlock47To48 = false;
constexpr bool kCaptureBlock39 = false;

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
UINT64 g_arena_base = 0;
std::atomic<unsigned> g_trace_count{0};
SRWLOCK g_trace_lock = SRWLOCK_INIT;
std::atomic<bool> g_copy_ready{false};
void *g_copy_kernel = nullptr;
void *g_fill_kernel = nullptr;
void *g_surface_kernel = nullptr;
void *g_texture_kernel = nullptr;

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

int dispatch_raw_copy(UINT64 source, UINT64 destination, UINT64 bytes) {
    if (!g_copy_ready.load() || g_copy_kernel == nullptr) return -1;
    CopyParams params{source, destination, static_cast<uint32_t>(bytes / 16), 0};
    void **vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
    auto bind = reinterpret_cast<BindKernel>(vtable[0xd8 / 8]);
    auto dispatch = reinterpret_cast<DispatchKernel>(vtable[0x140 / 8]);
    const int bind_result = bind(g_live_ngx_context, g_copy_kernel);
    if (bind_result != 0) return bind_result;
    struct ParameterDescriptor {
        void *vtable;
        void *blob;
        uint32_t bytes;
        uint32_t padding;
    } descriptor{
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_runtime) + 0xb28b8),
        &params, sizeof(params), 0,
    };
    return dispatch(
        g_live_ngx_context, &descriptor, g_live_command_context,
        (params.uint4_count + 255) / 256, 1, 1);
}

int dispatch_raw_fill(UINT64 destination, UINT64 bytes, uint32_t value) {
    if (!g_copy_ready.load() || g_fill_kernel == nullptr || bytes % 4) return -1;
    struct FillParams { UINT64 destination; uint32_t count; uint32_t value; } params{
        destination, static_cast<uint32_t>(bytes / 4), value,
    };
    void **vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
    auto bind = reinterpret_cast<BindKernel>(vtable[0xd8 / 8]);
    auto dispatch = reinterpret_cast<DispatchKernel>(vtable[0x140 / 8]);
    const int bind_result = bind(g_live_ngx_context, g_fill_kernel);
    if (bind_result != 0) return bind_result;
    struct ParameterDescriptor { void *vtable; void *blob; uint32_t bytes; uint32_t padding; } descriptor{
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_runtime) + 0xb28b8),
        &params, sizeof(params), 0,
    };
    return dispatch(g_live_ngx_context, &descriptor, g_live_command_context,
        (params.count + 255) / 256, 1, 1);
}

int dispatch_surface_rgba16f(
    UINT64 surface, UINT64 destination, uint32_t width, uint32_t height,
    uint32_t origin_x = 0, uint32_t origin_y = 0,
    uint32_t stride_x = 1, uint32_t stride_y = 1) {
    if (!g_copy_ready.load() || g_surface_kernel == nullptr) return -1;
    struct SurfaceParams {
        UINT64 surface, destination;
        uint32_t width, height, origin_x, origin_y, stride_x, stride_y;
    } params{surface, destination, width, height, origin_x, origin_y, stride_x, stride_y};
    void **vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
    auto bind = reinterpret_cast<BindKernel>(vtable[0xd8 / 8]);
    auto dispatch = reinterpret_cast<DispatchKernel>(vtable[0x140 / 8]);
    const int bind_result = bind(g_live_ngx_context, g_surface_kernel);
    if (bind_result != 0) return bind_result;
    struct ParameterDescriptor { void *vtable; void *blob; uint32_t bytes; uint32_t padding; } descriptor{
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_runtime) + 0xb28b8),
        &params, sizeof(params), 0,
    };
    return dispatch(g_live_ngx_context, &descriptor, g_live_command_context,
        (width + 15) / 16, (height + 15) / 16, 1);
}

int dispatch_texture_rgba(
    UINT64 texture, UINT64 destination, uint32_t output_width,
    uint32_t output_height, uint32_t source_width, uint32_t source_height,
    uint32_t origin_x, uint32_t origin_y, uint32_t stride_x, uint32_t stride_y) {
    if (!g_copy_ready.load() || g_texture_kernel == nullptr) return -1;
    struct TextureParams {
        UINT64 texture, destination;
        uint32_t output_width, output_height, source_width, source_height;
        uint32_t origin_x, origin_y, stride_x, stride_y;
    } params{texture, destination, output_width, output_height,
             source_width, source_height, origin_x, origin_y, stride_x, stride_y};
    void **vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
    auto bind = reinterpret_cast<BindKernel>(vtable[0xd8 / 8]);
    auto dispatch = reinterpret_cast<DispatchKernel>(vtable[0x140 / 8]);
    const int bind_result = bind(g_live_ngx_context, g_texture_kernel);
    if (bind_result != 0) return bind_result;
    struct ParameterDescriptor { void *vtable; void *blob; uint32_t bytes; uint32_t padding; } descriptor{
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_runtime) + 0xb28b8),
        &params, sizeof(params), 0,
    };
    return dispatch(g_live_ngx_context, &descriptor, g_live_command_context,
        (output_width + 15) / 16, (output_height + 15) / 16, 1);
}

int64_t hook_backend_launch(
    void *self, void *kernel, uint32_t gx, uint32_t gy, uint32_t gz,
    void *wrapper, uint64_t bytes, uint8_t flag) {
    void *blob = nullptr;
    if (wrapper != nullptr) std::memcpy(&blob, wrapper, sizeof(blob));
    if (g_launch_armed.load() && !g_launch_captured.exchange(true) && blob != nullptr) {
        std::memcpy(
            &g_live_ngx_context, static_cast<const uint8_t *>(self) + 0x08,
            sizeof(g_live_ngx_context));
        std::memcpy(
            &g_live_command_context, static_cast<const uint8_t *>(self) + 0x10,
            sizeof(g_live_command_context));
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
            UINT64 weight = 0;
            std::memcpy(&weight, static_cast<const uint8_t *>(blob) + 0x10, sizeof(weight));
            if (weight >= 22016) g_arena_base = weight - 22016;
        }
    }
    bool block48_inputs_pre_ok = false;
    bool block48_simple_pre_ok = false;
    bool block48_replay_ok = false;
    UINT64 block48_main_source = 0;
    UINT64 block48_output_source = 0;
    UINT64 block48_skip_source = 0;
    UINT64 block48_weight_source = 0;
    UINT64 block48_aux_source = 0;
    bool block66_pre_ok = false;
    UINT64 block66_main = 0, block66_output = 0;
    UINT64 block66_aux = 0, block66_skip = 0;
    bool block62_pre_ok = false;
    UINT64 block62_main = 0, block62_output = 0;
    UINT64 block62_skip = 0, block62_aux = 0;
    bool block56_pre_ok = false;
    UINT64 block56_main = 0, block56_output = 0, block56_skip = 0;
    bool block39_pre_ok = false;
    UINT64 block39_main = 0, block39_skip = 0, block39_output = 0;
    if (g_copy_ready.load() && blob != nullptr && bytes >= 0x50 && g_arena_base != 0) {
        UINT64 weight = 0;
        std::memcpy(&weight, static_cast<const uint8_t *>(blob) + 0x10, 8);
        if (bytes == 0x48) {
            UINT64 vit_weight = 0;
            std::memcpy(&vit_weight, static_cast<const uint8_t *>(blob) + 0x18, 8);
            if (vit_weight - g_arena_base == 23028224) {
                UINT64 input = 0;
                std::memcpy(&input, static_cast<const uint8_t *>(blob) + 0x00, 8);
                void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
                auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
                const int pre_sync = synchronize(
                    g_live_ngx_context, g_live_command_context, 0, 0);
                const int copy_result = pre_sync == 0 ? dispatch_raw_copy(
                    input, g_destination->GetGPUVirtualAddress(), 3ull << 20) : -1;
                const int sync_result = copy_result == 0 ? synchronize(
                    g_live_ngx_context, g_live_command_context, 0, 0) : -1;
                AcquireSRWLockExclusive(&g_trace_lock);
                if (FILE *file = _wfopen(kLogPath, L"ab")) {
                    std::fprintf(file,
                        "block31_input input=0x%llx pre_sync=%d copy=%d sync=%d\n",
                        static_cast<unsigned long long>(input), pre_sync,
                        copy_result, sync_result);
                    std::fclose(file);
                }
                ReleaseSRWLockExclusive(&g_trace_lock);
            }
        }
        if (kCompareBlock47To48 && bytes == 0x58 &&
            weight - g_arena_base == 140034048) {
            UINT64 source = 0;
            std::memcpy(&source, static_cast<const uint8_t *>(blob) + 0x00, 8);
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int pre_sync = synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0);
            const int copy_result = pre_sync == 0 ? dispatch_raw_copy(
                source, g_destination->GetGPUVirtualAddress() + (320ull << 20),
                5ull << 20) : -1;
            const int sync_result = copy_result == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block48_compare_main source=0x%llx pre_sync=%d copy=%d sync=%d\n",
                    static_cast<unsigned long long>(source), pre_sync,
                    copy_result, sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
        if (kCaptureBlock39 && bytes == 0x50) {
            UINT64 block39_weight = 0;
            std::memcpy(&block39_weight, static_cast<const uint8_t *>(blob) + 0x38, 8);
            if (block39_weight - g_arena_base == 123736576) {
                std::memcpy(&block39_main, static_cast<const uint8_t *>(blob) + 0x00, 8);
                std::memcpy(&block39_skip, static_cast<const uint8_t *>(blob) + 0x08, 8);
                std::memcpy(&block39_output, static_cast<const uint8_t *>(blob) + 0x30, 8);
                const UINT64 atlas = g_destination->GetGPUVirtualAddress();
                int operations[2]{};
                operations[0] = dispatch_raw_copy(
                    block39_main, atlas, 4ull << 20);
                operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                    block39_skip, atlas + (64ull << 20), 5ull << 20) : -1;
                void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
                auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
                const int sync_result = operations[1] == 0 ? synchronize(
                    g_live_ngx_context, g_live_command_context, 0, 0) : -1;
                block39_pre_ok = sync_result == 0;
                AcquireSRWLockExclusive(&g_trace_lock);
                if (FILE *file = _wfopen(kLogPath, L"ab")) {
                    std::fprintf(file,
                        "block39_pre main=0x%llx skip=0x%llx output=0x%llx copies=%d,%d sync=%d\n",
                        static_cast<unsigned long long>(block39_main),
                        static_cast<unsigned long long>(block39_skip),
                        static_cast<unsigned long long>(block39_output),
                        operations[0], operations[1], sync_result);
                    std::fclose(file);
                }
                ReleaseSRWLockExclusive(&g_trace_lock);
            }
        }
        if (kCaptureBlock48 && bytes == 0x58 &&
            weight - g_arena_base == 140034048) {
            std::memcpy(&block48_main_source, static_cast<const uint8_t *>(blob) + 0x00, 8);
            std::memcpy(&block48_output_source, static_cast<const uint8_t *>(blob) + 0x08, 8);
            std::memcpy(&block48_skip_source, static_cast<const uint8_t *>(blob) + 0x18, 8);
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            int operations[2]{};
            operations[0] = dispatch_raw_copy(
                block48_main_source - 0x2800, atlas, 64ull << 20);
            operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                block48_skip_source - 0x2800, atlas + (128ull << 20), 64ull << 20) : -1;
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int sync_result = operations[1] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            block48_simple_pre_ok = sync_result == 0;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block48_simple_pre main=0x%llx output=0x%llx skip=0x%llx copies=%d,%d sync=%d\n",
                    static_cast<unsigned long long>(block48_main_source),
                    static_cast<unsigned long long>(block48_output_source),
                    static_cast<unsigned long long>(block48_skip_source),
                    operations[0], operations[1], sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
        if (kCaptureBlock56 && bytes == 0x58 && weight - g_arena_base == 145744896) {
            std::memcpy(&block56_main, static_cast<const uint8_t *>(blob) + 0x00, 8);
            std::memcpy(&block56_output, static_cast<const uint8_t *>(blob) + 0x08, 8);
            std::memcpy(&block56_skip, static_cast<const uint8_t *>(blob) + 0x18, 8);
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            int operations[2]{};
            operations[0] = dispatch_raw_copy(
                block56_main - 0x2800, atlas, 64ull << 20);
            operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                block56_skip - 0x2800, atlas + (128ull << 20), 64ull << 20) : -1;
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int sync_result = operations[1] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            block56_pre_ok = sync_result == 0;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block56_pre main=0x%llx output=0x%llx skip=0x%llx copies=%d,%d sync=%d\n",
                    static_cast<unsigned long long>(block56_main),
                    static_cast<unsigned long long>(block56_output),
                    static_cast<unsigned long long>(block56_skip),
                    operations[0], operations[1], sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
        if (kCaptureBlock62 && bytes == 0x58 && weight - g_arena_base == 147025408) {
            std::memcpy(&block62_main, static_cast<const uint8_t *>(blob) + 0x00, 8);
            std::memcpy(&block62_output, static_cast<const uint8_t *>(blob) + 0x08, 8);
            std::memcpy(&block62_skip, static_cast<const uint8_t *>(blob) + 0x18, 8);
            std::memcpy(&block62_aux, static_cast<const uint8_t *>(blob) + 0x40, 8);
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            int operations[3]{};
            operations[0] = dispatch_raw_copy(block62_main - 0x2800, atlas, 64ull << 20);
            operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                block62_skip - 0x2800, atlas + (128ull << 20), 64ull << 20) : -1;
            operations[2] = operations[1] == 0 ? dispatch_raw_copy(
                block62_aux - 0x2800, atlas + (192ull << 20), 64ull << 20) : -1;
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int sync_result = operations[2] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            block62_pre_ok = sync_result == 0;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block62_pre main=0x%llx output=0x%llx skip=0x%llx aux=0x%llx copies=%d,%d,%d sync=%d\n",
                    static_cast<unsigned long long>(block62_main),
                    static_cast<unsigned long long>(block62_output),
                    static_cast<unsigned long long>(block62_skip),
                    static_cast<unsigned long long>(block62_aux),
                    operations[0], operations[1], operations[2], sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
        if (kCaptureBlock66 && bytes == 0x60 && weight - g_arena_base == 147281408) {
            std::memcpy(&block66_main, static_cast<const uint8_t *>(blob) + 0x00, 8);
            std::memcpy(&block66_output, static_cast<const uint8_t *>(blob) + 0x08, 8);
            std::memcpy(&block66_aux, static_cast<const uint8_t *>(blob) + 0x38, 8);
            std::memcpy(&block66_skip, static_cast<const uint8_t *>(blob) + 0x50, 8);
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            int operations[3]{};
            operations[0] = dispatch_raw_copy(
                block66_main - 0x2800, atlas, 64ull << 20);
            operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                block66_aux - 0x2800, atlas + (128ull << 20), 64ull << 20) : -1;
            operations[2] = operations[1] == 0 ? dispatch_raw_copy(
                block66_skip - 0x2800, atlas + (192ull << 20), 64ull << 20) : -1;
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int sync_result = operations[2] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            block66_pre_ok = sync_result == 0;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block66_pre main=0x%llx output=0x%llx aux=0x%llx skip=0x%llx copies=%d,%d,%d sync=%d\n",
                    static_cast<unsigned long long>(block66_main),
                    static_cast<unsigned long long>(block66_output),
                    static_cast<unsigned long long>(block66_aux),
                    static_cast<unsigned long long>(block66_skip),
                    operations[0], operations[1], operations[2], sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
        if (kCaptureBlock48Identity && weight - g_arena_base == 140034048) {
            auto get_kernel = reinterpret_cast<GetKernel>(
                reinterpret_cast<uintptr_t>(g_runtime) + kGetKernelRva);
            void *plain_kernel = get_kernel(
                self, "cc_tinlayout_fused_swin_8h_256_8_upsample_fp8",
                32, 8, 1, 0);
            void *tilesync_kernel = get_kernel(
                self, "cc_tinlayout_fused_swin_8h_256_8_upsample_tilesync_fp8",
                32, 8, 1, 0);
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block48_kernel live=%p plain=%p tilesync=%p\n",
                    kernel, plain_kernel, tilesync_kernel);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int sync_result = synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0);
            log_line("block48_pre_sync", sync_result);
            std::memcpy(&block48_main_source, static_cast<const uint8_t *>(blob) + 0x00, 8);
            std::memcpy(&block48_output_source, static_cast<const uint8_t *>(blob) + 0x08, 8);
            std::memcpy(&block48_weight_source, static_cast<const uint8_t *>(blob) + 0x10, 8);
            std::memcpy(&block48_skip_source, static_cast<const uint8_t *>(blob) + 0x18, 8);
            std::memcpy(&block48_aux_source, static_cast<const uint8_t *>(blob) + 0x40, 8);
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            constexpr UINT64 weight_bytes = 821248;
            const UINT64 weight_backup = atlas + (384ull << 20);
            const UINT64 identity_weight = atlas + (512ull << 20);
            int operations[10]{};
            operations[0] = sync_result == 0 ? dispatch_raw_copy(
                block48_output_source - 0x2800, atlas + (256ull << 20), 128ull << 20) : -1;
            operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                block48_weight_source, weight_backup, weight_bytes) : -1;
            operations[2] = operations[1] == 0 ? dispatch_raw_copy(
                block48_aux_source, atlas + (128ull << 20), 128ull << 20) : -1;
            const int backup_sync = operations[2] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            operations[3] = backup_sync == 0 ? dispatch_raw_fill(
                identity_weight, weight_bytes, 0) : -1;
            const int zero_sync = operations[3] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            operations[4] = zero_sync == 0 ? dispatch_raw_copy(
                block48_weight_source, identity_weight, 196608) : -1;
            operations[5] = operations[4] == 0 ? dispatch_raw_fill(
                identity_weight + 491300, 1340, 0x3c003c00u) : -1;
            operations[6] = operations[5] == 0 ? dispatch_raw_fill(
                identity_weight + 820256, 512, 0x3c003c00u) : -1;
            const int construct_sync = operations[6] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            operations[7] = construct_sync == 0 ? dispatch_raw_copy(
                identity_weight, block48_weight_source, weight_bytes) : -1;
            const int install_sync = operations[7] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            operations[8] = install_sync == 0 ? dispatch_raw_copy(
                block48_weight_source, atlas + (386ull << 20), weight_bytes) : -1;
            operations[9] = operations[8] == 0 ? dispatch_raw_fill(
                block48_output_source - 0x2800, 64ull << 20, 0xa5a5a5a5u) : -1;
            const int controlled_sync = operations[9] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            block48_inputs_pre_ok = controlled_sync == 0;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block48_identity_pre backup_output=%d backup_weight=%d backup_aux=%d backup_sync=%d zero_identity=%d zero_sync=%d copy_prefix=%d identity_ffn_region=%d attn_skip=%d construct_sync=%d install=%d install_sync=%d snapshot=%d fill_output=%d sync=%d\n",
                    operations[0], operations[1], operations[2], backup_sync,
                    operations[3], zero_sync, operations[4], operations[5],
                    operations[6], construct_sync, operations[7], install_sync,
                    operations[8], operations[9], controlled_sync);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
    }
    int64_t result = g_original_backend_launch(
        self, kernel, gx, gy, gz, wrapper, bytes, flag);
    if (result == 0 && block39_pre_ok) {
        void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
        auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
        const int pre_sync = synchronize(
            g_live_ngx_context, g_live_command_context, 0, 0);
        const int copy_result = pre_sync == 0 ? dispatch_raw_copy(
            block39_output,
            g_destination->GetGPUVirtualAddress() + (128ull << 20), 5ull << 20) : -1;
        const int sync_result = copy_result == 0 ? synchronize(
            g_live_ngx_context, g_live_command_context, 0, 0) : -1;
        AcquireSRWLockExclusive(&g_trace_lock);
        if (FILE *file = _wfopen(kLogPath, L"ab")) {
            std::fprintf(file, "block39_post pre_sync=%d output_copy=%d sync=%d\n",
                pre_sync, copy_result, sync_result);
            std::fclose(file);
        }
        ReleaseSRWLockExclusive(&g_trace_lock);
    }
    if (result == 0 && block48_simple_pre_ok) {
        const int copy_result = dispatch_raw_copy(
            block48_output_source - 0x2800,
            g_destination->GetGPUVirtualAddress() + (64ull << 20), 64ull << 20);
        void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
        auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
        const int sync_result = copy_result == 0 ? synchronize(
            g_live_ngx_context, g_live_command_context, 0, 0) : -1;
        AcquireSRWLockExclusive(&g_trace_lock);
        if (FILE *file = _wfopen(kLogPath, L"ab")) {
            std::fprintf(file, "block48_simple_post output_copy=%d sync=%d\n",
                copy_result, sync_result);
            std::fclose(file);
        }
        ReleaseSRWLockExclusive(&g_trace_lock);
    }
    if (result == 0 && block56_pre_ok) {
        const int copy_result = dispatch_raw_copy(
            block56_output - 0x2800,
            g_destination->GetGPUVirtualAddress() + (64ull << 20), 64ull << 20);
        void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
        auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
        const int sync_result = copy_result == 0 ? synchronize(
            g_live_ngx_context, g_live_command_context, 0, 0) : -1;
        AcquireSRWLockExclusive(&g_trace_lock);
        if (FILE *file = _wfopen(kLogPath, L"ab")) {
            std::fprintf(file, "block56_post output_copy=%d sync=%d\n",
                copy_result, sync_result);
            std::fclose(file);
        }
        ReleaseSRWLockExclusive(&g_trace_lock);
    }
    if (result == 0 && block62_pre_ok) {
        const int copy_result = dispatch_raw_copy(
            block62_output - 0x2800,
            g_destination->GetGPUVirtualAddress() + (64ull << 20), 64ull << 20);
        void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
        auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
        const int sync_result = copy_result == 0 ? synchronize(
            g_live_ngx_context, g_live_command_context, 0, 0) : -1;
        AcquireSRWLockExclusive(&g_trace_lock);
        if (FILE *file = _wfopen(kLogPath, L"ab")) {
            std::fprintf(file, "block62_post output_copy=%d sync=%d\n",
                copy_result, sync_result);
            std::fclose(file);
        }
        ReleaseSRWLockExclusive(&g_trace_lock);
    }
    if (result == 0 && g_copy_ready.load() && blob != nullptr && bytes >= 0x18 &&
        g_arena_base != 0) {
        UINT64 weight = 0, output = 0;
        const size_t weight_field = bytes == 0x48 ? 0x18 : 0x10;
        const size_t output_field = bytes == 0x48 ? 0x10 : 0x08;
        std::memcpy(&weight, static_cast<const uint8_t *>(blob) + weight_field, 8);
        std::memcpy(&output, static_cast<const uint8_t *>(blob) + output_field, 8);
        const UINT64 offset = weight - g_arena_base;
        UINT64 atlas_offset = UINT64_MAX;
        if (offset == 34566144) atlas_offset = 64ull << 20;
        else if (offset == 47154688) atlas_offset = 128ull << 20;
        else if (offset == 59743232) atlas_offset = 192ull << 20;
        else if (offset == 72331776) atlas_offset = 256ull << 20;
        if (atlas_offset != UINT64_MAX) {
            const UINT64 copy_bytes = 3ull << 20;
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int pre_sync = synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0);
            const int copy_result = pre_sync == 0 ? dispatch_raw_copy(
                bytes == 0x48 ? output : output - 0x2800,
                g_destination->GetGPUVirtualAddress() + atlas_offset,
                copy_bytes) : -1;
            const int sync_result = copy_result == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "decoder_stage_output weight_offset=%llu output=0x%llx atlas_offset=%llu pre_sync=%d copy=%d sync=%d\n",
                    static_cast<unsigned long long>(offset),
                    static_cast<unsigned long long>(output),
                    static_cast<unsigned long long>(atlas_offset),
                    pre_sync, copy_result, sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
    }
    if (result == 0 && block66_pre_ok) {
        const int copy_result = dispatch_raw_copy(
            block66_output - 0x2800,
            g_destination->GetGPUVirtualAddress() + (64ull << 20),
            64ull << 20);
        void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
        auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
        const int sync_result = copy_result == 0 ? synchronize(
            g_live_ngx_context, g_live_command_context, 0, 0) : -1;
        AcquireSRWLockExclusive(&g_trace_lock);
        if (FILE *file = _wfopen(kLogPath, L"ab")) {
            std::fprintf(file,
                "block66_post output_copy=%d sync=%d\n", copy_result, sync_result);
            std::fclose(file);
        }
        ReleaseSRWLockExclusive(&g_trace_lock);
    }
    if (result == 0 && g_copy_ready.load() && blob != nullptr && bytes >= 0x50) {
        UINT64 weight = 0;
        std::memcpy(&weight, static_cast<const uint8_t *>(blob) + 0x10, 8);
        const UINT64 offset = g_arena_base != 0 ? weight - g_arena_base : UINT64_MAX;
        UINT64 source = 0, atlas_offset = 0, copy_bytes = 0;
        if (offset == 124261888) {
            std::memcpy(&source, static_cast<const uint8_t *>(blob) + 0x40, 8);
            atlas_offset = 0; copy_bytes = 64ull << 20;
        } else if (offset == 147451904) {
            std::memcpy(&source, static_cast<const uint8_t *>(blob) + 0x08, 8);
            atlas_offset = 64ull << 20; copy_bytes = 32ull << 20;
        } else if (offset == 833536) {
            std::memcpy(&source, static_cast<const uint8_t *>(blob) + 0x08, 8);
            atlas_offset = 96ull << 20; copy_bytes = 16ull << 20;
        } else if (offset == 5912576) {
            std::memcpy(&source, static_cast<const uint8_t *>(blob) + 0x08, 8);
            atlas_offset = 112ull << 20; copy_bytes = 8ull << 20;
        }
        if (source != 0 && copy_bytes != 0) {
            const int copy_result = dispatch_raw_copy(
                source - 0x2800,
                g_destination->GetGPUVirtualAddress() + atlas_offset,
                copy_bytes);
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "skip_copy weight_offset=%llu source=0x%llx atlas_offset=%llu bytes=%llu result=%d\n",
                    static_cast<unsigned long long>(offset),
                    static_cast<unsigned long long>(source),
                    static_cast<unsigned long long>(atlas_offset),
                    static_cast<unsigned long long>(copy_bytes), copy_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
    }
    if (result == 0 && g_copy_ready.load() && blob != nullptr && bytes >= 0x50 &&
        g_arena_base != 0) {
        UINT64 weight = 0;
        std::memcpy(&weight, static_cast<const uint8_t *>(blob) + 0x10, 8);
        if (kCaptureBlock48Identity && weight - g_arena_base == 140034048) {
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            constexpr UINT64 weight_bytes = 821248;
            int operations[7]{};
            operations[0] = result == 0 && block48_inputs_pre_ok ? dispatch_raw_copy(
                block48_output_source - 0x2800, atlas, 64ull << 20) : -1;
            operations[1] = operations[0] == 0 ? dispatch_raw_copy(
                atlas + (384ull << 20), block48_weight_source, weight_bytes) : -1;
            operations[2] = operations[1] == 0 ? dispatch_raw_copy(
                atlas + (256ull << 20), block48_output_source - 0x2800, 128ull << 20) : -1;
            operations[3] = operations[2] == 0 ? dispatch_raw_copy(
                atlas + (128ull << 20), block48_aux_source, 128ull << 20) : -1;
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            operations[4] = operations[3] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            const int64_t normal_result = operations[4] == 0
                ? g_original_backend_launch(self, kernel, gx, gy, gz, wrapper, bytes, flag)
                : operations[4];
            result = normal_result;
            operations[5] = normal_result == 0 ? dispatch_raw_copy(
                block48_output_source - 0x2800, atlas + (64ull << 20), 64ull << 20) : -1;
            operations[6] = operations[5] == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            block48_replay_ok = operations[6] == 0;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block48_identity_post save_prefix=%d restore_weight=%d restore_output=%d restore_aux=%d sync=%d normal=%lld save_live=%d final_sync=%d\n",
                    operations[0], operations[1], operations[2], operations[3], operations[4],
                    static_cast<long long>(normal_result), operations[5], operations[6]);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
    }
    // Keep the block48 identity-prefix and the final decoder activation in one
    // atlas/readback.  block69 is the last raw-buffer layer before the separate
    // post backend, so this also gives a frame-coherent block70 main source.
    if (result == 0 && g_copy_ready.load() && blob != nullptr && bytes >= 0x18 &&
        g_arena_base != 0) {
        UINT64 weight = 0;
        std::memcpy(&weight, static_cast<const uint8_t *>(blob) + 0x10, 8);
        if (weight - g_arena_base == 147346432) {
            UINT64 output = 0;
            std::memcpy(&output, static_cast<const uint8_t *>(blob) + 0x08, 8);
            const UINT64 atlas = g_destination->GetGPUVirtualAddress();
            const int copy_result = dispatch_raw_copy(
                output - 0x2800, atlas + (400ull << 20), 64ull << 20);
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int sync_result = copy_result == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block69_same_frame output=0x%llx atlas_offset=%llu bytes=%llu copy=%d sync=%d\n",
                    static_cast<unsigned long long>(output), 400ull << 20,
                    64ull << 20, copy_result, sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
        }
    }
    if (result == 0 && g_copy_ready.load() && blob != nullptr && bytes == 0xb8 &&
        g_arena_base != 0) {
        UINT64 skip = 0, surface = 0, weight = 0, color = 0;
        std::memcpy(&skip, static_cast<const uint8_t *>(blob) + 0x08, 8);
        std::memcpy(&surface, static_cast<const uint8_t *>(blob) + 0x10, 8);
        std::memcpy(&weight, static_cast<const uint8_t *>(blob) + 0x18, 8);
        std::memcpy(&color, static_cast<const uint8_t *>(blob) + 0x38, 8);
        if (weight - g_arena_base == 147429888) {
            const int copy_result = dispatch_raw_copy(
                skip - 0x2800 + (64ull << 20),
                g_destination->GetGPUVirtualAddress() + (464ull << 20),
                64ull << 20);
            void **context_vtable = *reinterpret_cast<void ***>(g_live_ngx_context);
            auto synchronize = reinterpret_cast<ContextSync>(context_vtable[0x150 / 8]);
            const int surface_result = copy_result == 0 ? dispatch_surface_rgba16f(
                surface, g_destination->GetGPUVirtualAddress() + (592ull << 20),
                256, 144, 2304, 576, 1, 1) : -1;
            const int texture_result = surface_result == 0 ? dispatch_texture_rgba(
                color, g_destination->GetGPUVirtualAddress() + (593ull << 20),
                256, 144, 3840, 2176, 2304, 576, 1, 1) : -1;
            const int sync_result = texture_result == 0 ? synchronize(
                g_live_ngx_context, g_live_command_context, 0, 0) : -1;
            AcquireSRWLockExclusive(&g_trace_lock);
            if (FILE *file = _wfopen(kLogPath, L"ab")) {
                std::fprintf(file,
                    "block70_same_frame skip=0x%llx surface=0x%llx color=0x%llx weight=0x%llx skip_atlas=%llu surface_atlas=%llu color_atlas=%llu copy=%d surface_copy=%d texture_copy=%d sync=%d\n",
                    static_cast<unsigned long long>(skip),
                    static_cast<unsigned long long>(surface),
                    static_cast<unsigned long long>(color),
                    static_cast<unsigned long long>(weight), 464ull << 20,
                    592ull << 20, 593ull << 20, copy_result, surface_result,
                    texture_result, sync_result);
                std::fclose(file);
            }
            ReleaseSRWLockExclusive(&g_trace_lock);
            if (sync_result == 0 && g_queue != nullptr && !g_finished.exchange(true)) {
                g_queue->AddRef();
                if (HANDLE thread = CreateThread(
                        nullptr, 0, readback_worker, g_queue, 0, nullptr)) {
                    CloseHandle(thread);
                }
            }
        }
    }
    const unsigned trace = g_trace_count.fetch_add(1);
    if (trace < 512 && blob != nullptr && bytes <= 0x200) {
        UINT64 weight = 0;
        if (bytes >= 0x18) std::memcpy(
            &weight, static_cast<const uint8_t *>(blob) + 0x10, sizeof(weight));
        const bool arena_weight = g_arena_base != 0 &&
            weight >= g_arena_base && weight < g_arena_base + 147719680;
        AcquireSRWLockExclusive(&g_trace_lock);
        if (FILE *file = _wfopen(kTracePath, L"ab")) {
                std::fprintf(file,
                    "seq=%u weight=0x%llx arena=%u weight_offset=%llu grid=%u,%u,%u bytes=%llu flag=%u qwords=",
                    trace, static_cast<unsigned long long>(weight), arena_weight ? 1u : 0u,
                    arena_weight ? static_cast<unsigned long long>(weight - g_arena_base) : UINT64_MAX,
                    gx, gy, gz, static_cast<unsigned long long>(bytes), flag);
                for (uint64_t offset = 0; offset + 8 <= bytes; offset += 8) {
                    unsigned long long value = 0;
                    std::memcpy(&value, static_cast<const uint8_t *>(blob) + offset, 8);
                    std::fprintf(file, "%s0x%llx", offset == 0 ? "" : ",", value);
                }
                std::fprintf(file, "\n");
                std::fclose(file);
        }
        ReleaseSRWLockExclusive(&g_trace_lock);
    }
    return result;
}

bool create_destination() {
    if (g_device == nullptr || g_destination != nullptr) return g_destination != nullptr;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kProbeBytes;
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
    g_copy_kernel = kernel;
    g_fill_kernel = get_kernel(backend, "fill_raw_buffer", 256, 1, 1, 0);
    g_surface_kernel = get_kernel(backend, "capture_surface_rgba16f", 16, 16, 1, 0);
    g_texture_kernel = get_kernel(backend, "capture_texture_rgba", 16, 16, 1, 0);
    if (g_fill_kernel == nullptr || g_surface_kernel == nullptr || g_texture_kernel == nullptr) return;

    CopyParams input_params{
        input_source,
        g_destination->GetGPUVirtualAddress() + (528ull << 20),
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
    g_copy_ready.store(true);

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
        DeleteFileW(kTracePath);
        reshade::register_event<reshade::addon_event::init_device>(on_init_device);
        HANDLE thread = CreateThread(nullptr, 0, &hook_worker, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
        reshade::unregister_addon(instance);
    }
    return TRUE;
}
