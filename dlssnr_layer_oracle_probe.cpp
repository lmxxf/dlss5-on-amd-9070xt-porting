#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "MinHook.h"

extern "C" __declspec(dllexport) const char *NAME = "DLSSNR Layer Oracle Probe";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Captures block1 input/output and the complete runtime-packed weight arena.";

namespace {
constexpr unsigned kReShadeApiVersion = 18;
constexpr uintptr_t kForward1HRva = 0x637b0;
constexpr uintptr_t kBackendLaunchRva = 0x449a0;
constexpr size_t kCaptureBytes = 1024 * 1024;
constexpr size_t kWeightArenaBytes = 147719680;
constexpr unsigned long long kBlock1ArenaOffset = 22016;
constexpr wchar_t kLogPath[] = LR"(D:\DLSSNR-Lab\logs\layer-oracle.txt)";
constexpr wchar_t kInputPath[] = LR"(D:\DLSSNR-Lab\logs\block1-input.bin)";
constexpr wchar_t kOutputPath[] = LR"(D:\DLSSNR-Lab\logs\block1-output.bin)";
constexpr wchar_t kWeightArenaPath[] = LR"(D:\DLSSNR-Lab\logs\runtime-weight-arena.bin)";
constexpr wchar_t kForwardMetaPath[] = LR"(D:\DLSSNR-Lab\logs\layer-forward-meta.txt)";

using RegisterAddon = BOOL (*)(void *, unsigned);
using UnregisterAddon = void (*)(void *);
using Forward1H = void (*)(void *, void *, void *, void *, int, int);
using CuCtxSynchronize = int (*)();
using CuMemcpyDtoH = int (*)(void *, unsigned long long, size_t);
using CuMemGetAddressRange = int (*)(
    unsigned long long *, size_t *, unsigned long long);
using CuInit = int (*)(unsigned);
using CuDeviceGet = int (*)(int *, int);
using CuDevicePrimaryCtxRetain = int (*)(void **, int);
using CuCtxPushCurrent = int (*)(void *);
using CuPointerGetAttribute = int (*)(void *, int, unsigned long long);
using CuLaunchKernel = int (*)(
    void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
    unsigned, void *, void **, void **);
struct CuLaunchConfig {
    unsigned grid_x, grid_y, grid_z;
    unsigned block_x, block_y, block_z;
    unsigned shared_memory;
    void *stream;
    void *attributes;
    unsigned attribute_count;
};
using CuLaunchKernelEx = int (*)(const CuLaunchConfig *, void *, void **, void **);
using CuGetProcAddress = int (*)(
    const char *, void **, int, unsigned long long, int *);
using BackendLaunch = int64_t (*)(
    void *, void *, uint32_t, uint32_t, uint32_t, void *, uint64_t, uint8_t);

HMODULE g_module = nullptr;
UnregisterAddon g_unregister_addon = nullptr;
Forward1H g_original = nullptr;
std::atomic<unsigned> g_calls{0};
std::atomic<bool> g_launch_armed{false};
std::atomic<bool> g_launch_captured{false};
CuLaunchKernel g_original_launch = nullptr;
CuLaunchKernel g_original_launch_ptsz = nullptr;
CuLaunchKernelEx g_original_launch_ex = nullptr;
CuLaunchKernelEx g_original_launch_ex_ptsz = nullptr;
CuGetProcAddress g_original_get_proc_address = nullptr;
BackendLaunch g_original_backend_launch = nullptr;
int g_width = 0;
int g_height = 0;

void write_binary(const wchar_t *path, const std::vector<uint8_t> &bytes) {
    FILE *file = _wfopen(path, L"wb");
    if (file != nullptr) {
        std::fwrite(bytes.data(), 1, bytes.size(), file);
        std::fclose(file);
    }
}

void capture_oracle(void *inputs, void *outputs, int width, int height) {
    auto **input_begin = *reinterpret_cast<void ***>(inputs);
    auto **output_begin = *reinterpret_cast<void ***>(outputs);
    void *input = input_begin == nullptr ? nullptr : input_begin[0];
    void *output = output_begin == nullptr ? nullptr : output_begin[0];

    HMODULE cuda = GetModuleHandleW(L"nvcuda.dll");
    auto synchronize = cuda == nullptr ? nullptr : reinterpret_cast<CuCtxSynchronize>(
        GetProcAddress(cuda, "cuCtxSynchronize"));
    auto copy_to_host = cuda == nullptr ? nullptr : reinterpret_cast<CuMemcpyDtoH>(
        GetProcAddress(cuda, "cuMemcpyDtoH_v2"));
    auto initialize = cuda == nullptr ? nullptr : reinterpret_cast<CuInit>(
        GetProcAddress(cuda, "cuInit"));
    auto get_device = cuda == nullptr ? nullptr : reinterpret_cast<CuDeviceGet>(
        GetProcAddress(cuda, "cuDeviceGet"));
    auto retain_primary = cuda == nullptr ? nullptr : reinterpret_cast<CuDevicePrimaryCtxRetain>(
        GetProcAddress(cuda, "cuDevicePrimaryCtxRetain"));
    auto push_context = cuda == nullptr ? nullptr : reinterpret_cast<CuCtxPushCurrent>(
        GetProcAddress(cuda, "cuCtxPushCurrent_v2"));

    int init_result = -1;
    int device_result = -1;
    int retain_result = -1;
    int push_result = -1;
    int sync_result = -1;
    int input_result = -1;
    int output_result = -1;
    std::vector<uint8_t> input_bytes(kCaptureBytes);
    std::vector<uint8_t> output_bytes(kCaptureBytes);
    if (initialize != nullptr && get_device != nullptr && retain_primary != nullptr &&
        push_context != nullptr && synchronize != nullptr && copy_to_host != nullptr &&
        input != nullptr && output != nullptr) {
        init_result = initialize(0);
        int device = 0;
        void *primary = nullptr;
        device_result = init_result == 0 ? get_device(&device, 0) : -1;
        retain_result = device_result == 0 ? retain_primary(&primary, device) : -1;
        push_result = retain_result == 0 ? push_context(primary) : -1;
        sync_result = push_result == 0 ? synchronize() : -1;
        if (sync_result == 0) {
            input_result = copy_to_host(
                input_bytes.data(), reinterpret_cast<unsigned long long>(input), input_bytes.size());
            output_result = copy_to_host(
                output_bytes.data(), reinterpret_cast<unsigned long long>(output), output_bytes.size());
        }
    }
    if (input_result == 0) {
        write_binary(kInputPath, input_bytes);
    }
    if (output_result == 0) {
        write_binary(kOutputPath, output_bytes);
    }

    FILE *log = _wfopen(kLogPath, L"wb");
    if (log != nullptr) {
        std::fprintf(
            log,
            "width=%d\nheight=%d\ninput=%p\noutput=%p\n"
            "capture_bytes=%zu\ncuInit=%d\ncuDeviceGet=%d\n"
            "cuDevicePrimaryCtxRetain=%d\ncuCtxPushCurrent=%d\ncuCtxSynchronize=%d\n"
            "cuMemcpyDtoH_input=%d\n"
            "cuMemcpyDtoH_output=%d\n",
            width, height, input, output, kCaptureBytes,
            init_result, device_result, retain_result, push_result,
            sync_result, input_result, output_result);
        std::fclose(log);
    }
}

void capture_launch_oracle(
    void **kernel_params,
    unsigned grid_x, unsigned grid_y, unsigned grid_z,
    unsigned block_x, unsigned block_y, unsigned block_z) {
    if (kernel_params == nullptr || kernel_params[0] == nullptr) {
        return;
    }
    auto *blob = static_cast<const uint8_t *>(kernel_params[0]);
    unsigned long long input = 0;
    unsigned long long output = 0;
    unsigned long long weight = 0;
    std::memcpy(&input, blob + 0x00, sizeof(input));
    std::memcpy(&output, blob + 0x08, sizeof(output));
    std::memcpy(&weight, blob + 0x10, sizeof(weight));

    HMODULE cuda = GetModuleHandleW(L"nvcuda.dll");
    auto synchronize = reinterpret_cast<CuCtxSynchronize>(
        GetProcAddress(cuda, "cuCtxSynchronize"));
    auto copy_to_host = reinterpret_cast<CuMemcpyDtoH>(
        GetProcAddress(cuda, "cuMemcpyDtoH_v2"));
    auto get_address_range = reinterpret_cast<CuMemGetAddressRange>(
        GetProcAddress(cuda, "cuMemGetAddressRange_v2"));
    auto initialize = reinterpret_cast<CuInit>(GetProcAddress(cuda, "cuInit"));
    auto get_device = reinterpret_cast<CuDeviceGet>(GetProcAddress(cuda, "cuDeviceGet"));
    auto retain_primary = reinterpret_cast<CuDevicePrimaryCtxRetain>(
        GetProcAddress(cuda, "cuDevicePrimaryCtxRetain"));
    auto push_context = reinterpret_cast<CuCtxPushCurrent>(
        GetProcAddress(cuda, "cuCtxPushCurrent_v2"));
    auto pointer_get_attribute = reinterpret_cast<CuPointerGetAttribute>(
        GetProcAddress(cuda, "cuPointerGetAttribute"));
    int init_result = initialize == nullptr ? -1 : initialize(0);
    int device = 0;
    void *primary = nullptr;
    int device_result = init_result == 0 && get_device != nullptr
        ? get_device(&device, 0) : -1;
    int retain_result = device_result == 0 && retain_primary != nullptr
        ? retain_primary(&primary, device) : -1;
    void *owner_context = nullptr;
    constexpr int kCuPointerAttributeContext = 1;
    int pointer_context_result = init_result == 0 && pointer_get_attribute != nullptr
        ? pointer_get_attribute(
            &owner_context, kCuPointerAttributeContext, weight) : -1;
    void *copy_context = pointer_context_result == 0 && owner_context != nullptr
        ? owner_context : primary;
    int push_result = copy_context != nullptr && push_context != nullptr
        ? push_context(copy_context) : -1;
    int sync_result = push_result == 0 && synchronize != nullptr
        ? synchronize() : -1;
    int input_result = -1;
    int output_result = -1;
    int arena_result = -1;
    int range_result = -1;
    unsigned long long allocation_base = 0;
    size_t allocation_size = 0;
    std::vector<uint8_t> input_bytes(kCaptureBytes);
    std::vector<uint8_t> output_bytes(kCaptureBytes);
    if (sync_result == 0 && copy_to_host != nullptr) {
        input_result = copy_to_host(input_bytes.data(), input, input_bytes.size());
        output_result = copy_to_host(output_bytes.data(), output, output_bytes.size());
        if (get_address_range != nullptr) {
            range_result = get_address_range(
                &allocation_base, &allocation_size, weight);
        }
        const unsigned long long calculated_base =
            weight >= kBlock1ArenaOffset ? weight - kBlock1ArenaOffset : 0;
        const bool calculated_base_is_plausible = calculated_base != 0 &&
            (calculated_base & 0xffffULL) == 0;
        const bool allocation_matches = range_result == 0 &&
            allocation_base == calculated_base &&
            allocation_size >= kWeightArenaBytes;
        if (allocation_matches || calculated_base_is_plausible) {
            std::vector<uint8_t> arena_bytes(kWeightArenaBytes);
            arena_result = copy_to_host(
                arena_bytes.data(), calculated_base, arena_bytes.size());
            if (arena_result == 0) {
                write_binary(kWeightArenaPath, arena_bytes);
            }
        }
    }
    if (input_result == 0) {
        write_binary(kInputPath, input_bytes);
    }
    if (output_result == 0) {
        write_binary(kOutputPath, output_bytes);
    }
    FILE *log = _wfopen(kLogPath, L"wb");
    if (log != nullptr) {
        std::fprintf(
            log,
            "width=%d\nheight=%d\ninput=0x%llx\noutput=0x%llx\nweight=0x%llx\n"
            "grid=%u,%u,%u\nblock=%u,%u,%u\ncapture_bytes=%zu\n"
            "cuInit=%d\ncuDeviceGet=%d\ncuDevicePrimaryCtxRetain=%d\n"
            "cuPointerGetAttribute_CONTEXT=%d\nowner_context=%p\n"
            "copy_context=%p\ncuCtxPushCurrent=%d\ncuCtxSynchronize=%d\n"
            "cuMemcpyDtoH_input=%d\ncuMemcpyDtoH_output=%d\n"
            "calculated_arena_base=0x%llx\ncuMemGetAddressRange=%d\n"
            "allocation_base=0x%llx\nallocation_size=%zu\n"
            "cuMemcpyDtoH_arena=%d\narena_bytes=%zu\n",
            g_width, g_height, input, output, weight,
            grid_x, grid_y, grid_z, block_x, block_y, block_z,
            kCaptureBytes, init_result, device_result, retain_result,
            pointer_context_result, owner_context, copy_context,
            push_result, sync_result, input_result, output_result,
            weight >= kBlock1ArenaOffset ? weight - kBlock1ArenaOffset : 0,
            range_result, allocation_base, allocation_size,
            arena_result, kWeightArenaBytes);
        std::fclose(log);
    }
}

int hook_launch_kernel(
    void *function,
    unsigned grid_x, unsigned grid_y, unsigned grid_z,
    unsigned block_x, unsigned block_y, unsigned block_z,
    unsigned shared_memory, void *stream, void **kernel_params, void **extra) {
    const bool capture = g_launch_armed.load() && !g_launch_captured.exchange(true);
    const int result = g_original_launch(
        function, grid_x, grid_y, grid_z, block_x, block_y, block_z,
        shared_memory, stream, kernel_params, extra);
    if (capture && result == 0) {
        capture_launch_oracle(
            kernel_params, grid_x, grid_y, grid_z, block_x, block_y, block_z);
    }
    return result;
}

int hook_launch_kernel_ptsz(
    void *function,
    unsigned grid_x, unsigned grid_y, unsigned grid_z,
    unsigned block_x, unsigned block_y, unsigned block_z,
    unsigned shared_memory, void *stream, void **kernel_params, void **extra) {
    const bool capture = g_launch_armed.load() && !g_launch_captured.exchange(true);
    const int result = g_original_launch_ptsz(
        function, grid_x, grid_y, grid_z, block_x, block_y, block_z,
        shared_memory, stream, kernel_params, extra);
    if (capture && result == 0) {
        capture_launch_oracle(
            kernel_params, grid_x, grid_y, grid_z, block_x, block_y, block_z);
    }
    return result;
}

int launch_ex_common(
    CuLaunchKernelEx original,
    const CuLaunchConfig *config, void *function, void **kernel_params, void **extra) {
    const bool capture = g_launch_armed.load() && !g_launch_captured.exchange(true);
    const int result = original(config, function, kernel_params, extra);
    if (capture && result == 0 && config != nullptr) {
        capture_launch_oracle(
            kernel_params,
            config->grid_x, config->grid_y, config->grid_z,
            config->block_x, config->block_y, config->block_z);
    }
    return result;
}

int hook_launch_kernel_ex(
    const CuLaunchConfig *config, void *function, void **kernel_params, void **extra) {
    return launch_ex_common(g_original_launch_ex, config, function, kernel_params, extra);
}

int hook_launch_kernel_ex_ptsz(
    const CuLaunchConfig *config, void *function, void **kernel_params, void **extra) {
    return launch_ex_common(g_original_launch_ex_ptsz, config, function, kernel_params, extra);
}

int64_t hook_backend_launch(
    void *self, void *kernel, uint32_t grid_x, uint32_t grid_y,
    uint32_t grid_z, void *wrapper, uint64_t bytes, uint8_t flag) {
    const bool capture = g_launch_armed.load() && !g_launch_captured.exchange(true);
    const int64_t result = g_original_backend_launch(
        self, kernel, grid_x, grid_y, grid_z, wrapper, bytes, flag);
    if (capture && result == 0 && wrapper != nullptr) {
        void *blob = nullptr;
        std::memcpy(&blob, wrapper, sizeof(blob));
        if (blob != nullptr) {
            void *kernel_params[] = {blob};
            capture_launch_oracle(
                kernel_params, grid_x, grid_y, grid_z, 0, 0, 0);
        }
    }
    return result;
}

int hook_get_proc_address(
    const char *symbol, void **function, int version,
    unsigned long long flags, int *status) {
    const int result = g_original_get_proc_address(symbol, function, version, flags, status);
    if (result != 0 || symbol == nullptr || function == nullptr || *function == nullptr) {
        return result;
    }
    if (std::strcmp(symbol, "cuLaunchKernelEx") == 0) {
        if ((flags & 2) != 0) {
            g_original_launch_ex_ptsz = reinterpret_cast<CuLaunchKernelEx>(*function);
            *function = reinterpret_cast<void *>(&hook_launch_kernel_ex_ptsz);
        } else {
            g_original_launch_ex = reinterpret_cast<CuLaunchKernelEx>(*function);
            *function = reinterpret_cast<void *>(&hook_launch_kernel_ex);
        }
    } else if (std::strcmp(symbol, "cuLaunchKernel") == 0) {
        if ((flags & 2) != 0) {
            g_original_launch_ptsz = reinterpret_cast<CuLaunchKernel>(*function);
            *function = reinterpret_cast<void *>(&hook_launch_kernel_ptsz);
        } else {
            g_original_launch = reinterpret_cast<CuLaunchKernel>(*function);
            *function = reinterpret_cast<void *>(&hook_launch_kernel);
        }
    }
    return result;
}

void hook_forward(
    void *self, void *inputs, void *outputs, void *context, int width, int height) {
    const unsigned call = g_calls.fetch_add(1);
    if (call == 0) {
        g_width = width;
        g_height = height;
        void *state = nullptr;
        std::memcpy(&state, static_cast<const uint8_t *>(self) + 0x178, sizeof(state));
        void *kernel = nullptr;
        void *kernel_function = nullptr;
        void *kernel_handle = nullptr;
        void *backend = nullptr;
        void *backend_vtable = nullptr;
        void *backend_launch = nullptr;
        if (state != nullptr) {
            std::memcpy(&kernel, static_cast<const uint8_t *>(state) + 0x08, sizeof(kernel));
            if (kernel != nullptr) {
                std::memcpy(&kernel_function, kernel, sizeof(kernel_function));
                std::memcpy(
                    &kernel_handle,
                    static_cast<const uint8_t *>(kernel) + 0x08,
                    sizeof(kernel_handle));
            }
            std::memcpy(
                &backend,
                static_cast<const uint8_t *>(state) + 0x48,
                sizeof(backend));
            if (backend != nullptr) {
                std::memcpy(&backend_vtable, backend, sizeof(backend_vtable));
                if (backend_vtable != nullptr) {
                    std::memcpy(
                        &backend_launch,
                        static_cast<const uint8_t *>(backend_vtable) + 0x28,
                        sizeof(backend_launch));
                }
            }
        }
        HMODULE backend_module = nullptr;
        if (backend_launch != nullptr && GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(backend_launch), &backend_module)) {
        }
        HMODULE kernel_module = nullptr;
        if (kernel_function != nullptr) {
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(kernel_function), &kernel_module);
        }
        FILE *meta = _wfopen(kForwardMetaPath, L"wb");
        if (meta != nullptr) {
            std::fprintf(
                meta,
                "self=%p\nstate=%p\nkernel=%p\nkernel_function=%p\nkernel_handle=%p\n"
                "kernel_module=%p\nkernel_rva=0x%llx\nbackend=%p\nbackend_vtable=%p\n"
                "backend_launch=%p\nbackend_module=%p\nbackend_rva=0x%llx\n"
                "width=%d\nheight=%d\n",
                self, state, kernel, kernel_function, kernel_handle, kernel_module,
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(kernel_function) -
                    reinterpret_cast<uintptr_t>(kernel_module)),
                backend, backend_vtable, backend_launch,
                backend_module,
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(backend_launch) -
                    reinterpret_cast<uintptr_t>(backend_module)),
                width, height);
            std::fclose(meta);
        }
        g_launch_armed.store(true);
    }
    g_original(self, inputs, outputs, context, width, height);
    if (call == 0) {
        g_launch_armed.store(false);
    }
}

DWORD WINAPI hook_worker(void *) {
    HMODULE cuda = nullptr;
    for (unsigned attempt = 0; attempt < 600 && cuda == nullptr; ++attempt) {
        cuda = GetModuleHandleW(L"nvcuda.dll");
        if (cuda == nullptr) {
            Sleep(100);
        }
    }
    if (cuda == nullptr || MH_Initialize() != MH_OK) {
        return 1;
    }
    void *get_proc_target = reinterpret_cast<void *>(
        GetProcAddress(cuda, "cuGetProcAddress_v2"));
    if (get_proc_target == nullptr) {
        get_proc_target = reinterpret_cast<void *>(GetProcAddress(cuda, "cuGetProcAddress"));
    }
    if (get_proc_target == nullptr || MH_CreateHook(
            get_proc_target,
            reinterpret_cast<void *>(&hook_get_proc_address),
            reinterpret_cast<void **>(&g_original_get_proc_address)) != MH_OK ||
        MH_EnableHook(get_proc_target) != MH_OK) {
        return 2;
    }

    HMODULE runtime = nullptr;
    for (unsigned attempt = 0; attempt < 600 && runtime == nullptr; ++attempt) {
        runtime = GetModuleHandleW(L"nvngx_dlssnr.dll");
        if (runtime == nullptr) {
            Sleep(100);
        }
    }
    if (runtime == nullptr) {
        return 3;
    }
    void *target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kForward1HRva);
    void *backend_target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kBackendLaunchRva);
    if (MH_CreateHook(
            backend_target,
            reinterpret_cast<void *>(&hook_backend_launch),
            reinterpret_cast<void **>(&g_original_backend_launch)) != MH_OK) {
        return 4;
    }
    if (MH_CreateHook(
            target,
            reinterpret_cast<void *>(&hook_forward),
            reinterpret_cast<void **>(&g_original)) != MH_OK) {
        return 5;
    }
    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK ? 0 : 6;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&hook_worker), &pinned)) {
            return FALSE;
        }
        HMODULE reshade = GetModuleHandleW(L"d3d12.dll");
        auto register_addon = reshade == nullptr ? nullptr : reinterpret_cast<RegisterAddon>(
            GetProcAddress(reshade, "ReShadeRegisterAddon"));
        g_unregister_addon = reshade == nullptr ? nullptr : reinterpret_cast<UnregisterAddon>(
            GetProcAddress(reshade, "ReShadeUnregisterAddon"));
        if (register_addon == nullptr || g_unregister_addon == nullptr ||
            !register_addon(instance, kReShadeApiVersion)) {
            return FALSE;
        }
        HANDLE thread = CreateThread(nullptr, 0, &hook_worker, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH && g_unregister_addon != nullptr) {
        g_unregister_addon(g_module);
    }
    return TRUE;
}
