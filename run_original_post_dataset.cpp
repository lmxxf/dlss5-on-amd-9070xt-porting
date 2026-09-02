#include <cuda.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *message = nullptr;
        cuGetErrorString(result, &message);
        std::fprintf(stderr, "%s: %d %s\n", name, result,
                     message ? message : "?");
        std::exit(1);
    }
}

static std::vector<unsigned char> read_file(const char *path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) std::exit(2);
    size_t size = static_cast<size_t>(stream.tellg());
    stream.seekg(0);
    std::vector<unsigned char> bytes(size);
    stream.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}

int main(int argc, char **argv) {
    if (argc < 7 || argc > 9) {
        std::fprintf(stderr,
            "usage: %s cubin symbol weights blend samples.u8 output.rgba32f "
            "[scan-half-count [ablate|head]]\n"
            "       %s cubin symbol weights blend samples.u8 features.f32 features\n"
            "       %s cubin symbol weights blend samples.u8 features.f32 global-skip-features\n"
            "samples: repeated [2048-byte main][512-byte skip] records\n",
            argv[0], argv[0], argv[0]);
        return 2;
    }
    auto weights = read_file(argv[3]);
    auto blend = read_file(argv[4]);
    auto samples = read_file(argv[5]);
    constexpr size_t main_bytes = 2048;
    constexpr size_t skip_bytes = 512;
    constexpr size_t sample_bytes = main_bytes + skip_bytes;
    if (weights.size() != 21808 || blend.size() != 2 || samples.empty() ||
        samples.size() % sample_bytes) return 2;
    const bool global_skip_features =
        argc == 8 && !std::strcmp(argv[7], "global-skip-features");
    const bool feature_mode = argc == 8 &&
        (!std::strcmp(argv[7], "features") || global_skip_features);
    const bool scan_weights = argc >= 8 && !feature_mode;
    const bool ablate_weights = argc == 9 && !std::strcmp(argv[8], "ablate");
    const bool head_weights = argc == 9 && !std::strcmp(argv[8], "head");
    if (argc == 9 && !ablate_weights && !head_weights) return 2;
    const size_t input_count = samples.size() / sample_bytes;
    const size_t count = scan_weights ? std::strtoull(argv[7], nullptr, 0)
                                      : input_count;
    const size_t scan_limit = ablate_weights ? 10904 : (head_weights ? 512 : 10336);
    if (!count || (scan_weights && (input_count != 1 || count > scan_limit))) return 2;

    check("cuInit", cuInit(0));
    CUdevice device;
    check("cuDeviceGet", cuDeviceGet(&device, 0));
    CUcontext context;
    check("cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain(&context, device));
    check("cuCtxSetCurrent", cuCtxSetCurrent(context));
    CUmodule module;
    check("cuModuleLoad", cuModuleLoad(&module, argv[1]));
    CUfunction function;
    check("cuModuleGetFunction", cuModuleGetFunction(&function, module, argv[2]));

    CUdeviceptr main_device, skip_device, weights_device, blend_device;
    check("main alloc", cuMemAlloc(&main_device, 1 << 20));
    check("skip alloc", cuMemAlloc(&skip_device, 1 << 20));
    check("weights alloc", cuMemAlloc(&weights_device, weights.size()));
    check("blend alloc", cuMemAlloc(&blend_device, 512));
    if (!scan_weights && !feature_mode)
        check("weights upload", cuMemcpyHtoD(weights_device, weights.data(), weights.size()));
    check("blend upload", cuMemcpyHtoD(blend_device, blend.data(), blend.size()));

    CUDA_ARRAY3D_DESCRIPTOR output_desc{};
    output_desc.Width = output_desc.Height = 8;
    output_desc.Format = CU_AD_FORMAT_FLOAT;
    output_desc.NumChannels = 4;
    output_desc.Flags = CUDA_ARRAY3D_SURFACE_LDST;
    CUarray output_array;
    check("output array", cuArray3DCreate(&output_array, &output_desc));
    CUDA_RESOURCE_DESC output_resource{};
    output_resource.resType = CU_RESOURCE_TYPE_ARRAY;
    output_resource.res.array.hArray = output_array;
    CUsurfObject output_surface;
    check("output surface", cuSurfObjectCreate(&output_surface, &output_resource));

    std::vector<float> color(8 * 8 * 4, 0.5f);
    for (size_t pixel = 0; pixel < 64; ++pixel) color[pixel * 4 + 3] = 1.0f;
    CUDA_ARRAY3D_DESCRIPTOR texture_desc{};
    texture_desc.Width = texture_desc.Height = 8;
    texture_desc.Format = CU_AD_FORMAT_FLOAT;
    texture_desc.NumChannels = 4;
    CUarray texture_array;
    check("texture array", cuArray3DCreate(&texture_array, &texture_desc));
    CUDA_MEMCPY2D texture_copy{};
    texture_copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    texture_copy.srcHost = color.data();
    texture_copy.srcPitch = 8 * 16;
    texture_copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    texture_copy.dstArray = texture_array;
    texture_copy.WidthInBytes = 8 * 16;
    texture_copy.Height = 8;
    check("texture upload", cuMemcpy2D(&texture_copy));
    CUDA_RESOURCE_DESC texture_resource{};
    texture_resource.resType = CU_RESOURCE_TYPE_ARRAY;
    texture_resource.res.array.hArray = texture_array;
    CUDA_TEXTURE_DESC texture_options{};
    texture_options.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_options.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_options.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texture_options.flags = CU_TRSF_NORMALIZED_COORDINATES;
    CUtexObject texture;
    check("texture object", cuTexObjectCreate(
        &texture, &texture_resource, &texture_options, nullptr));

    alignas(8) unsigned char params[0xb8]{};
    std::memcpy(params + 0x00, &main_device, 8);
    std::memcpy(params + 0x08, &skip_device, 8);
    std::memcpy(params + 0x10, &output_surface, 8);
    std::memcpy(params + 0x18, &weights_device, 8);
    const int width = global_skip_features ? 256 : 8;
    const int height = global_skip_features ? 144 : 8;
    std::memcpy(params + 0x20, &height, 4);
    std::memcpy(params + 0x24, &width, 4);
    const float input_scale = 0.03125f;
    const int rgb_mode = 1;
    std::memcpy(params + 0x30, &input_scale, 4);
    std::memcpy(params + 0x34, &rgb_mode, 4);
    std::memcpy(params + 0x38, &texture, 8);
    const float transform[6] = {0, 0, 1, 1, 1, 1};
    std::memcpy(params + 0x40, transform, sizeof(transform));
    std::memcpy(params + 0x68, &blend_device, 8);
    const float one = 1.0f;
    std::memcpy(params + 0xa4, &one, 4);
    std::memcpy(params + 0xa8, &one, 4);
    std::memcpy(params + 0xac, &width, 4);
    std::memcpy(params + 0xb0, &height, 4);
    void *arguments[] = {params};

    std::vector<float> output(count * 8 * 8 * (feature_mode ? 32 : 4));
    std::vector<unsigned char> scan_weight;
    if (feature_mode) {
        scan_weight.assign(weights.size(), 0);
        std::copy(weights.begin(), weights.begin() + 10392 * 2,
                  scan_weight.begin());
    } else if (scan_weights) {
        if (ablate_weights) {
            scan_weight = weights;
        } else if (head_weights) {
            scan_weight.assign(weights.size(), 0);
            // Preserve the complete shifted body through its eight-slot tail
            // padding; scan only the two packed out-conv blocks.
            std::copy(weights.begin(), weights.begin() + 10392 * 2,
                      scan_weight.begin());
        } else {
            scan_weight.assign(weights.size(), 0);
            // The last 568 FP16 slots are the post-specific output head. Keep
            // them live while scanning one body slot at a time.
            std::copy(weights.end() - 1136, weights.end(), scan_weight.end() - 1136);
        }
    }
    CUDA_MEMCPY2D download{};
    download.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    download.srcArray = output_array;
    download.dstMemoryType = CU_MEMORYTYPE_HOST;
    download.dstPitch = 8 * 16;
    download.WidthInBytes = 8 * 16;
    download.Height = 8;
    std::vector<float> rgba_tile(8 * 8 * 4);
    std::vector<float> positive_tile(8 * 8);
    for (size_t sample = 0; sample < count; ++sample) {
        const auto *record = samples.data() +
            (scan_weights ? 0 : sample) * sample_bytes;
        if (scan_weights) {
            const size_t weight_slot = (head_weights ? 10392 : 0) + sample;
            const unsigned short value = ablate_weights ? 0 : 0x3c00;
            std::memcpy(scan_weight.data() + weight_slot * 2, &value, 2);
            if (sample) {
                const size_t previous_slot = weight_slot - 1;
                if (ablate_weights) {
                    std::memcpy(scan_weight.data() + previous_slot * 2,
                                weights.data() + previous_slot * 2, 2);
                } else {
                    const unsigned short zero = 0;
                    std::memcpy(scan_weight.data() + previous_slot * 2, &zero, 2);
                }
            }
            check("scan weight upload", cuMemcpyHtoD(
                weights_device, scan_weight.data(), scan_weight.size()));
        }
        check("main clear", cuMemsetD8(main_device, 0, 1 << 20));
        check("skip clear", cuMemsetD8(skip_device, 0, 1 << 20));
        if (global_skip_features) {
            check("global skip bank0", cuMemcpyHtoD(skip_device, record, 1024));
            check("global skip bank1", cuMemcpyHtoD(
                skip_device + 32768, record + 1024, 1024));
        } else {
            check("main upload", cuMemcpyHtoD(main_device, record, main_bytes));
            check("skip upload", cuMemcpyHtoD(skip_device, record + main_bytes, skip_bytes));
        }
        if (feature_mode) {
            for (size_t channel = 0; channel < 32; ++channel) {
                const size_t block = channel < 16 ? 10392 : 10648;
                const size_t local = channel & 15;
                const size_t slot = block + (local / 4) * 8 + local % 4;
                const unsigned short positive_half = 0x1400;
                const unsigned short negative_half = 0x9400;
                const unsigned short zero_half = 0;
                constexpr float probe = 1.0f / 1024.0f;
                std::memcpy(scan_weight.data() + slot * 2, &positive_half, 2);
                check("feature weight upload", cuMemcpyHtoD(
                    weights_device, scan_weight.data(), scan_weight.size()));
                check("feature launch", cuLaunchKernel(
                    function, 1, 1, 1, 32, 2, 1, 0, nullptr, arguments, nullptr));
                check("feature sync", cuCtxSynchronize());
                download.dstHost = rgba_tile.data();
                check("feature download", cuMemcpy2D(&download));
                for (size_t pixel = 0; pixel < 64; ++pixel)
                    positive_tile[pixel] = rgba_tile[pixel * 4];
                std::memcpy(scan_weight.data() + slot * 2, &negative_half, 2);
                check("feature weight upload", cuMemcpyHtoD(
                    weights_device, scan_weight.data(), scan_weight.size()));
                check("feature launch", cuLaunchKernel(
                    function, 1, 1, 1, 32, 2, 1, 0, nullptr, arguments, nullptr));
                check("feature sync", cuCtxSynchronize());
                check("feature download", cuMemcpy2D(&download));
                for (size_t pixel = 0; pixel < 64; ++pixel)
                    output[(sample * 64 + pixel) * 32 + channel] =
                        std::abs(positive_tile[pixel] - 0.5f) >=
                        std::abs(0.5f - rgba_tile[pixel * 4])
                        ? (positive_tile[pixel] - 0.5f) / probe
                        : (0.5f - rgba_tile[pixel * 4]) / probe;
                std::memcpy(scan_weight.data() + slot * 2, &zero_half, 2);
            }
            continue;
        }
        check("launch", cuLaunchKernel(
            function, 1, 1, 1, 32, 2, 1, 0, nullptr, arguments, nullptr));
        check("sync", cuCtxSynchronize());
        download.dstHost = output.data() + sample * 8 * 8 * 4;
        check("download", cuMemcpy2D(&download));
    }
    std::ofstream stream(argv[6], std::ios::binary);
    stream.write(reinterpret_cast<const char *>(output.data()),
                 output.size() * sizeof(float));
    const auto [low, high] = std::minmax_element(output.begin(), output.end());
    size_t finite = 0;
    for (float value : output) finite += std::isfinite(value);
    std::printf("samples=%zu scan_weights=%d features=%d finite=%zu/%zu "
                "range=%.9g..%.9g\n", count, scan_weights, feature_mode,
                finite, output.size(), *low, *high);
    return stream ? 0 : 3;
}
