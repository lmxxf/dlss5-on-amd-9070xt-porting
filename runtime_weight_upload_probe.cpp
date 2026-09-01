#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "reshade.hpp"

extern "C" __declspec(dllexport) const char *NAME = "DLSSNR Weight Upload Probe";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Captures host-visible uploads into large DLSSNR candidate buffers.";

namespace {
constexpr uint64_t kMinimumBufferBytes = 64ull * 1024 * 1024;
constexpr uint64_t kMaximumBufferBytes = 512ull * 1024 * 1024;
constexpr uint64_t kMaximumCapturedBytes = 512ull * 1024 * 1024;
constexpr wchar_t kLogPath[] = LR"(D:\DLSSNR-Lab\logs\weight-upload-probe.txt)";
std::atomic<uint64_t> g_captured_bytes{0};
SRWLOCK g_file_lock = SRWLOCK_INIT;
struct ActiveMap {
    reshade::api::device *device = nullptr;
    reshade::api::resource resource{};
    uint64_t offset = 0;
    uint64_t size = 0;
    const void *data = nullptr;
};
ActiveMap g_active_maps[16]{};

bool is_candidate(reshade::api::device *device, reshade::api::resource resource) {
    const reshade::api::resource_desc desc = device->get_resource_desc(resource);
    return desc.type == reshade::api::resource_type::buffer &&
        desc.buffer.size >= kMinimumBufferBytes &&
        desc.buffer.size <= kMaximumBufferBytes;
}

void append_log(
    const char *event, reshade::api::resource resource,
    uint64_t resource_size, uint64_t offset, uint64_t size,
    const wchar_t *dump_path) {
    AcquireSRWLockExclusive(&g_file_lock);
    if (FILE *file = _wfopen(kLogPath, L"ab")) {
        std::fprintf(
            file,
            "%s resource=0x%llx resource_size=%llu offset=%llu size=%llu dump=%ls\n",
            event,
            static_cast<unsigned long long>(resource.handle),
            static_cast<unsigned long long>(resource_size),
            static_cast<unsigned long long>(offset),
            static_cast<unsigned long long>(size),
            dump_path == nullptr ? L"-" : dump_path);
        std::fclose(file);
    }
    ReleaseSRWLockExclusive(&g_file_lock);
}

void capture_upload(
    const char *event, reshade::api::device *device,
    reshade::api::resource resource, uint64_t offset,
    uint64_t size, const void *data) {
    if (data == nullptr || size == 0 || !is_candidate(device, resource)) {
        return;
    }
    const uint64_t previous = g_captured_bytes.fetch_add(size);
    if (previous >= kMaximumCapturedBytes || size > kMaximumCapturedBytes - previous) {
        return;
    }
    const reshade::api::resource_desc desc = device->get_resource_desc(resource);
    wchar_t path[MAX_PATH]{};
    std::swprintf(
        path, MAX_PATH,
        LR"(D:\DLSSNR-Lab\logs\weight-upload-%016llx.bin)",
        static_cast<unsigned long long>(resource.handle));
    AcquireSRWLockExclusive(&g_file_lock);
    FILE *file = _wfopen(path, L"r+b");
    if (file == nullptr) {
        file = _wfopen(path, L"w+b");
    }
    if (file != nullptr) {
        _fseeki64(file, static_cast<__int64>(offset), SEEK_SET);
        std::fwrite(data, 1, static_cast<size_t>(size), file);
        std::fclose(file);
    }
    ReleaseSRWLockExclusive(&g_file_lock);
    append_log(event, resource, desc.buffer.size, offset, size, path);
}

void on_init_resource(
    reshade::api::device *device,
    const reshade::api::resource_desc &desc,
    const reshade::api::subresource_data *initial_data,
    reshade::api::resource_usage,
    reshade::api::resource resource) {
    if (desc.type == reshade::api::resource_type::buffer &&
        desc.buffer.size >= kMinimumBufferBytes &&
        desc.buffer.size <= kMaximumBufferBytes) {
        append_log("init", resource, desc.buffer.size, 0, 0, nullptr);
        if (initial_data != nullptr) {
            capture_upload(
                "initial_data", device, resource, 0,
                desc.buffer.size, initial_data->data);
        }
    }
}

bool on_update_buffer_region(
    reshade::api::device *device, const void *data,
    reshade::api::resource dest, uint64_t dest_offset, uint64_t size) {
    capture_upload("update", device, dest, dest_offset, size, data);
    return false;
}

bool on_copy_buffer_region(
    reshade::api::command_list *command_list,
    reshade::api::resource source, uint64_t source_offset,
    reshade::api::resource dest, uint64_t dest_offset, uint64_t size) {
    reshade::api::device *device = command_list->get_device();
    if (!is_candidate(device, dest) || size == 0) {
        return false;
    }
    const reshade::api::resource_desc dest_desc = device->get_resource_desc(dest);
    append_log("copy", dest, dest_desc.buffer.size, dest_offset, size, nullptr);
    return false;
}

void on_map_buffer_region(
    reshade::api::device *device, reshade::api::resource resource,
    uint64_t offset, uint64_t size, reshade::api::map_access,
    void **data) {
    if (data == nullptr || *data == nullptr || !is_candidate(device, resource)) {
        return;
    }
    AcquireSRWLockExclusive(&g_file_lock);
    for (ActiveMap &entry : g_active_maps) {
        if (entry.data == nullptr) {
            entry = {device, resource, offset, size, *data};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_file_lock);
}

void on_unmap_buffer_region(
    reshade::api::device *device, reshade::api::resource resource) {
    ActiveMap captured{};
    AcquireSRWLockExclusive(&g_file_lock);
    for (ActiveMap &entry : g_active_maps) {
        if (entry.data != nullptr && entry.resource == resource) {
            captured = entry;
            entry = {};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_file_lock);
    if (captured.data != nullptr) {
        const reshade::api::resource_desc desc = device->get_resource_desc(resource);
        const uint64_t bytes = captured.size == UINT64_MAX
            ? desc.buffer.size - captured.offset
            : captured.size;
        capture_upload("unmap", device, resource, captured.offset, bytes, captured.data);
    }
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        DeleteFileW(kLogPath);
        if (!reshade::register_addon(instance)) {
            return FALSE;
        }
        reshade::register_event<reshade::addon_event::init_resource>(on_init_resource);
        reshade::register_event<reshade::addon_event::update_buffer_region>(
            on_update_buffer_region);
        reshade::register_event<reshade::addon_event::copy_buffer_region>(
            on_copy_buffer_region);
        reshade::register_event<reshade::addon_event::map_buffer_region>(
            on_map_buffer_region);
        reshade::register_event<reshade::addon_event::unmap_buffer_region>(
            on_unmap_buffer_region);
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(
            on_unmap_buffer_region);
        reshade::unregister_event<reshade::addon_event::map_buffer_region>(
            on_map_buffer_region);
        reshade::unregister_event<reshade::addon_event::copy_buffer_region>(
            on_copy_buffer_region);
        reshade::unregister_event<reshade::addon_event::update_buffer_region>(
            on_update_buffer_region);
        reshade::unregister_event<reshade::addon_event::init_resource>(on_init_resource);
        reshade::unregister_addon(instance);
    }
    return TRUE;
}
