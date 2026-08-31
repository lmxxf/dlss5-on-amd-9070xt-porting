#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

extern "C" __declspec(dllexport) const char *NAME = "DLSSNR Runtime Probe";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Dumps the live DLSSNR CCNetwork after BuildActiveNetwork succeeds.";

namespace {
constexpr unsigned kReShadeApiVersion = 18;
constexpr uintptr_t kBuildActiveNetworkRva = 0x1f570;
constexpr wchar_t kOutputPath[] = L"D:\\DLSSNR-Lab\\logs\\runtime-network.txt";

using RegisterAddon = BOOL (*)(void *, unsigned);
using UnregisterAddon = void (*)(void *);
using BuildActiveNetwork = uint64_t (*)(void *, void *);

HMODULE g_module = nullptr;
UnregisterAddon g_unregister_addon = nullptr;
BuildActiveNetwork g_original = nullptr;
std::atomic<bool> g_dumped{false};

template <typename T>
T read_at(const void *base, size_t offset) {
    T value{};
    SIZE_T copied = 0;
    if (base == nullptr ||
        !ReadProcessMemory(
            GetCurrentProcess(),
            static_cast<const uint8_t *>(base) + offset,
            &value,
            sizeof(T),
            &copied) ||
        copied != sizeof(T)) {
        return T{};
    }
    return value;
}

size_t copy_msvc_string(const void *string_object, char *output, size_t output_size) {
    const size_t length = read_at<uint64_t>(string_object, 0x10);
    const uint64_t capacity = read_at<uint64_t>(string_object, 0x18);
    const char *source = capacity > 15
        ? read_at<const char *>(string_object, 0)
        : static_cast<const char *>(string_object);
    if (source == nullptr || output_size == 0 || length >= output_size) {
        return 0;
    }
    SIZE_T copied = 0;
    if (!ReadProcessMemory(
            GetCurrentProcess(), source, output, length, &copied) ||
        copied != length) {
        return 0;
    }
    output[length] = '\0';
    return length;
}

void dump_qwords(FILE *file, const void *address, size_t bytes) {
    const size_t count = bytes / sizeof(uint64_t);
    for (size_t index = 0; index < count; ++index) {
        std::fprintf(
            file,
            "%s0x%016llx",
            index == 0 ? "" : ",",
            static_cast<unsigned long long>(read_at<uint64_t>(address, index * 8)));
    }
}

void dump_network(void *manager, uint64_t result) {
    if (g_dumped.exchange(true)) {
        return;
    }

    FILE *file = _wfopen(kOutputPath, L"wb");
    if (file == nullptr) {
        g_dumped.store(false);
        return;
    }

    void *network = read_at<void *>(manager, 0x48);
    std::fprintf(file, "result=0x%llx\n", static_cast<unsigned long long>(result));
    std::fprintf(file, "manager=%p\nnetwork=%p\n", manager, network);
    if (network == nullptr) {
        std::fclose(file);
        return;
    }

    auto **block_begin = read_at<void **>(network, 0xf8);
    auto **block_end = read_at<void **>(network, 0x100);
    const ptrdiff_t block_count = block_begin != nullptr &&
            reinterpret_cast<uintptr_t>(block_end) >= reinterpret_cast<uintptr_t>(block_begin)
        ? static_cast<ptrdiff_t>(
            (reinterpret_cast<uintptr_t>(block_end) - reinterpret_cast<uintptr_t>(block_begin)) /
            sizeof(void *))
        : 0;
    std::fprintf(file, "block_count=%lld\n", static_cast<long long>(block_count));
    if (block_count < 1 || block_count > 1024) {
        std::fclose(file);
        return;
    }

    for (ptrdiff_t block_index = 0; block_index < block_count; ++block_index) {
        void *block = read_at<void *>(block_begin, block_index * sizeof(void *));
        if (block == nullptr) {
            std::fprintf(file, "block[%lld]=<unreadable>\n", static_cast<long long>(block_index));
            continue;
        }
        auto **layer_begin = read_at<void **>(block, 0xe8);
        auto **layer_end = read_at<void **>(block, 0xf0);
        const ptrdiff_t layer_count = layer_begin != nullptr &&
                reinterpret_cast<uintptr_t>(layer_end) >= reinterpret_cast<uintptr_t>(layer_begin)
            ? static_cast<ptrdiff_t>(
                (reinterpret_cast<uintptr_t>(layer_end) - reinterpret_cast<uintptr_t>(layer_begin)) /
                sizeof(void *))
            : -1;
        std::fprintf(
            file,
            "block[%lld]=%p layer_count=%lld\n",
            static_cast<long long>(block_index),
            block,
            static_cast<long long>(layer_count));
        if (layer_count < 0 || layer_count > 64) {
            continue;
        }

        for (ptrdiff_t layer_index = 0; layer_index < layer_count; ++layer_index) {
            void *layer = read_at<void *>(layer_begin, layer_index * sizeof(void *));
            if (layer == nullptr) {
                std::fprintf(file, "  layer[%lld]=<unreadable>\n", static_cast<long long>(layer_index));
                continue;
            }
            char name[256]{};
            const size_t name_length = copy_msvc_string(
                static_cast<uint8_t *>(layer) + 8, name, sizeof(name));
            const char *display_name = name_length == 0 ? "<unreadable>" : name;
            const size_t display_name_length = name_length == 0
                ? std::strlen(display_name)
                : name_length;
            void *state = read_at<void *>(layer, 0x178);
            std::fprintf(
                file,
                "  layer[%lld]=%p vtable=%p name=%.*s state=%p",
                static_cast<long long>(layer_index),
                layer,
                read_at<void *>(layer, 0),
                static_cast<int>(display_name_length),
                display_name,
                state);
            if (state != nullptr) {
                std::fprintf(file, " state_qwords=");
                // Layer state objects are heterogeneous and some contain only
                // the leading flat-weight GPU VA. Do not scan past that field.
                dump_qwords(file, state, sizeof(uint64_t));
            }
            std::fprintf(file, "\n");
        }
    }
    std::fflush(file);
    std::fclose(file);
}

uint64_t hook_build_active_network(void *manager, void *command_context) {
    const uint64_t result = g_original(manager, command_context);
    dump_network(manager, result);
    return result;
}

DWORD WINAPI hook_worker(void *) {
    HMODULE runtime = nullptr;
    for (unsigned attempt = 0; attempt < 600 && runtime == nullptr; ++attempt) {
        runtime = GetModuleHandleW(L"nvngx_dlssnr.dll");
        if (runtime == nullptr) {
            Sleep(100);
        }
    }
    if (runtime == nullptr || MH_Initialize() != MH_OK) {
        return 1;
    }

    void *target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kBuildActiveNetworkRva);
    if (MH_CreateHook(
            target,
            reinterpret_cast<void *>(&hook_build_active_network),
            reinterpret_cast<void **>(&g_original)) != MH_OK) {
        return 2;
    }
    if (MH_EnableHook(target) != MH_OK) {
        return 3;
    }
    return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&hook_worker),
                &pinned)) {
            return FALSE;
        }
        HMODULE reshade = GetModuleHandleW(L"d3d12.dll");
        if (reshade == nullptr) {
            return FALSE;
        }
        auto register_addon = reinterpret_cast<RegisterAddon>(
            GetProcAddress(reshade, "ReShadeRegisterAddon"));
        g_unregister_addon = reinterpret_cast<UnregisterAddon>(
            GetProcAddress(reshade, "ReShadeUnregisterAddon"));
        if (register_addon == nullptr || g_unregister_addon == nullptr ||
            !register_addon(instance, kReShadeApiVersion)) {
            return FALSE;
        }
        HANDLE thread = CreateThread(nullptr, 0, &hook_worker, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_unregister_addon != nullptr) {
            g_unregister_addon(g_module);
        }
    }
    return TRUE;
}
