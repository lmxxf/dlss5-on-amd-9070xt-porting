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
constexpr uintptr_t kVitQkvForwardRva = 0x72e40;
constexpr uintptr_t kVitContractForwardRva = 0x72620;
constexpr uintptr_t kVitExpandForwardRva = 0x72840;
constexpr uintptr_t kBackendLaunchRva = 0x449a0;
constexpr uintptr_t kVitAttentionForwardRva = 0x72440;
constexpr uintptr_t kVitProjectionForwardRva = 0x72b60;
constexpr uintptr_t kFused8HForwardRva = 0x69c70;
constexpr wchar_t kOutputPath[] = L"D:\\DLSSNR-Lab\\logs\\runtime-network.txt";
constexpr wchar_t kVitCallPath[] = L"D:\\DLSSNR-Lab\\logs\\vit-qkv-call.txt";
constexpr wchar_t kVitContractPath[] = L"D:\\DLSSNR-Lab\\logs\\vit-contract-call.txt";
constexpr wchar_t kVitExpandPath[] = L"D:\\DLSSNR-Lab\\logs\\vit-expand-call.txt";
constexpr wchar_t kVitLaunchPath[] = L"D:\\DLSSNR-Lab\\logs\\vit-launch-blobs.txt";

using RegisterAddon = BOOL (*)(void *, unsigned);
using UnregisterAddon = void (*)(void *);
using BuildActiveNetwork = uint64_t (*)(void *, void *);
using VitQkvForward = void (*)(void *, void *, void *, void *, int, int);
using BackendLaunch = int64_t (*)(void *, void *, uint32_t, uint32_t, uint32_t, void *, uint64_t, uint8_t);

HMODULE g_module = nullptr;
UnregisterAddon g_unregister_addon = nullptr;
BuildActiveNetwork g_original = nullptr;
VitQkvForward g_vit_qkv_original = nullptr;
VitQkvForward g_vit_contract_original = nullptr;
VitQkvForward g_vit_expand_original = nullptr;
VitQkvForward g_vit_attention_original = nullptr;
VitQkvForward g_vit_projection_original = nullptr;
VitQkvForward g_fused8h_original = nullptr;
std::atomic<unsigned> g_fused8h_calls{0};
BackendLaunch g_backend_launch_original = nullptr;
std::atomic<bool> g_dumped{false};
std::atomic<bool> g_vit_dumped{false};
std::atomic<bool> g_vit_contract_dumped{false};
std::atomic<bool> g_vit_expand_dumped{false};
std::atomic<int> g_vit_phase{0};

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

void dump_region(FILE *file, const char *label, const void *address, size_t bytes) {
    std::fprintf(file, "    %s=%p bytes=%zu\n", label, address, bytes);
    for (size_t offset = 0; offset < bytes; offset += 32) {
        std::fprintf(file, "      +%04zx:", offset);
        const size_t row_bytes = bytes - offset < 32 ? bytes - offset : 32;
        for (size_t index = 0; index < row_bytes; ++index) {
            std::fprintf(
                file,
                " %02x",
                static_cast<unsigned>(read_at<uint8_t>(address, offset + index)));
        }
        std::fprintf(file, "\n");
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
            // Deep-snapshot only the currently audited QKV layer and the two
            // final ABI gaps. The broad network walk stays conservative.
            // Keep the broad network walk conservative, but snapshot the
            // complete small host-side objects for these two known layers.
            const bool is_split_qkv = block_index == 23 && layer_index == 2;
            const bool is_final_gap =
                (block_index == 66 || block_index == 70) && layer_index == 0;
            if (is_split_qkv || is_final_gap) {
                dump_region(file, "layer_object", layer, 0x200);
                if (state != nullptr) {
                    dump_region(file, "layer_state", state, 0x200);
                }
            }
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

struct PointerVector { void **begin; void **end; void **capacity; };

void hook_vit_qkv(void *self, void *inputs, void *outputs, void *context, int width, int height) {
    if (!g_vit_dumped.exchange(true)) {
        PointerVector in = read_at<PointerVector>(inputs, 0);
        PointerVector out = read_at<PointerVector>(outputs, 0);
        FILE *file = _wfopen(kVitCallPath, L"wb");
        if (file != nullptr) {
            const ptrdiff_t ni = in.begin && in.end ? in.end - in.begin : -1;
            const ptrdiff_t no = out.begin && out.end ? out.end - out.begin : -1;
            std::fprintf(file, "self=%p width=%d height=%d inputs=%td outputs=%td\n", self, width, height, ni, no);
            for (ptrdiff_t i = 0; i < ni && i < 16; ++i) std::fprintf(file, "input[%td]=%p\n", i, read_at<void *>(in.begin, i * 8));
            for (ptrdiff_t i = 0; i < no && i < 16; ++i) std::fprintf(file, "output[%td]=%p\n", i, read_at<void *>(out.begin, i * 8));
            std::fclose(file);
        }
    }
    g_vit_phase.store(3); g_vit_qkv_original(self, inputs, outputs, context, width, height); g_vit_phase.store(0);
}

void hook_vit_contract(void *self, void *inputs, void *outputs, void *context, int width, int height) {
    if (!g_vit_contract_dumped.exchange(true)) {
        PointerVector in = read_at<PointerVector>(inputs, 0), out = read_at<PointerVector>(outputs, 0);
        FILE *file = _wfopen(kVitContractPath, L"wb");
        if (file != nullptr) {
            const ptrdiff_t ni = in.begin && in.end ? in.end - in.begin : -1, no = out.begin && out.end ? out.end - out.begin : -1;
            std::fprintf(file, "self=%p width=%d height=%d inputs=%td outputs=%td\n", self, width, height, ni, no);
            for (ptrdiff_t i=0;i<ni&&i<16;++i) std::fprintf(file,"input[%td]=%p\n",i,read_at<void*>(in.begin,i*8));
            for (ptrdiff_t i=0;i<no&&i<16;++i) std::fprintf(file,"output[%td]=%p\n",i,read_at<void*>(out.begin,i*8));
            std::fclose(file);
        }
    }
    g_vit_phase.store(2); g_vit_contract_original(self, inputs, outputs, context, width, height); g_vit_phase.store(0);
}

void hook_vit_expand(void *self, void *inputs, void *outputs, void *context, int width, int height) {
    if (!g_vit_expand_dumped.exchange(true)) {
        PointerVector in = read_at<PointerVector>(inputs, 0), out = read_at<PointerVector>(outputs, 0);
        FILE *file = _wfopen(kVitExpandPath, L"wb");
        if (file != nullptr) {
            const ptrdiff_t ni = in.begin && in.end ? in.end - in.begin : -1, no = out.begin && out.end ? out.end - out.begin : -1;
            std::fprintf(file, "self=%p width=%d height=%d inputs=%td outputs=%td\n", self, width, height, ni, no);
            for (ptrdiff_t i=0;i<ni&&i<16;++i) std::fprintf(file,"input[%td]=%p\n",i,read_at<void*>(in.begin,i*8));
            for (ptrdiff_t i=0;i<no&&i<16;++i) std::fprintf(file,"output[%td]=%p\n",i,read_at<void*>(out.begin,i*8));
            std::fclose(file);
        }
    }
    g_vit_phase.store(1); g_vit_expand_original(self, inputs, outputs, context, width, height); g_vit_phase.store(0);
}

void hook_vit_attention(void *self,void *inputs,void *outputs,void *context,int width,int height){g_vit_phase.store(4);g_vit_attention_original(self,inputs,outputs,context,width,height);g_vit_phase.store(0);}
void hook_vit_projection(void *self,void *inputs,void *outputs,void *context,int width,int height){g_vit_phase.store(5);g_vit_projection_original(self,inputs,outputs,context,width,height);g_vit_phase.store(0);}
void hook_fused8h(void*self,void*inputs,void*outputs,void*context,int width,int height){if(g_fused8h_calls.fetch_add(1)==9){g_vit_phase.store(6);g_fused8h_original(self,inputs,outputs,context,width,height);g_vit_phase.store(0);}else g_fused8h_original(self,inputs,outputs,context,width,height);}

int64_t hook_backend_launch(void *self, void *kernel, uint32_t gx, uint32_t gy, uint32_t gz, void *wrapper, uint64_t bytes, uint8_t flag) {
    const int phase = g_vit_phase.load();
    if (phase != 0 && wrapper != nullptr && bytes <= 0x100) {
        void *blob = read_at<void *>(wrapper, 0);
        FILE *file = _wfopen(kVitLaunchPath, L"ab");
        if (file != nullptr) {
            std::fprintf(file,"phase=%d kernel=%p grid=%u,%u,%u bytes=%llu flag=%u wrapper=%p blob=%p\n",phase,kernel,gx,gy,gz,(unsigned long long)bytes,(unsigned)flag,wrapper,blob);
            if (phase == 2) dump_region(file,"kernel_object",kernel,0x100);
            if (blob != nullptr) dump_region(file,"params",blob,(size_t)bytes);
            std::fclose(file);
        }
    }
    return g_backend_launch_original(self,kernel,gx,gy,gz,wrapper,bytes,flag);
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
    void *vit_target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kVitQkvForwardRva);
    void *contract_target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kVitContractForwardRva);
    void *expand_target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kVitExpandForwardRva);
    void *backend_target = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(runtime) + kBackendLaunchRva);
    void *attention_target=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(runtime)+kVitAttentionForwardRva);
    void *projection_target=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(runtime)+kVitProjectionForwardRva);
    void *fused8h_target=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(runtime)+kFused8HForwardRva);
    if (MH_CreateHook(
            target,
            reinterpret_cast<void *>(&hook_build_active_network),
            reinterpret_cast<void **>(&g_original)) != MH_OK) {
        return 2;
    }
    if (MH_CreateHook(
            vit_target,
            reinterpret_cast<void *>(&hook_vit_qkv),
            reinterpret_cast<void **>(&g_vit_qkv_original)) != MH_OK) {
        return 3;
    }
    if (MH_CreateHook(contract_target,reinterpret_cast<void *>(&hook_vit_contract),reinterpret_cast<void **>(&g_vit_contract_original)) != MH_OK) return 4;
    if (MH_CreateHook(expand_target,reinterpret_cast<void *>(&hook_vit_expand),reinterpret_cast<void **>(&g_vit_expand_original)) != MH_OK) return 5;
    if (MH_CreateHook(backend_target,reinterpret_cast<void *>(&hook_backend_launch),reinterpret_cast<void **>(&g_backend_launch_original)) != MH_OK) return 6;
    if(MH_CreateHook(attention_target,reinterpret_cast<void*>(&hook_vit_attention),reinterpret_cast<void**>(&g_vit_attention_original))!=MH_OK)return 7;
    if(MH_CreateHook(projection_target,reinterpret_cast<void*>(&hook_vit_projection),reinterpret_cast<void**>(&g_vit_projection_original))!=MH_OK)return 8;
    if(MH_CreateHook(fused8h_target,reinterpret_cast<void*>(&hook_fused8h),reinterpret_cast<void**>(&g_fused8h_original))!=MH_OK)return 9;
    DeleteFileW(kVitCallPath);
    DeleteFileW(kVitContractPath);
    DeleteFileW(kVitExpandPath);
    DeleteFileW(kVitLaunchPath);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        return 10;
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
