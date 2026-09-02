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

static std::vector<unsigned char> read_file(const char *path, size_t bytes) {
    std::vector<unsigned char> result(bytes);
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char *>(result.data()), bytes)) {
        std::fprintf(stderr, "cannot read %zu bytes from %s\n", bytes, path);
        std::exit(2);
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc < 11 || argc > 14) {
        std::fprintf(stderr,
            "usage: %s cubin symbol main skip weights blend color output "
            "width height [texture-mask=1] [rgb-mode=1] [input-scale=0.03125]\n",
            argv[0]);
        return 2;
    }

    const char *cubin_path = argv[1];
    const char *symbol = argv[2];
    const char *output_path = argv[8];
    const int width = std::atoi(argv[9]);
    const int height = std::atoi(argv[10]);
    const int texture_mask = argc > 11 ? std::atoi(argv[11]) : 1;
    const int rgb_mode = argc > 12 ? std::atoi(argv[12]) : 1;
    const float input_scale = argc > 13 ? std::strtof(argv[13], nullptr)
                                        : 0.03125f;
    if (width <= 0 || height <= 0) {
        std::fprintf(stderr, "invalid dimensions %dx%d\n", width, height);
        return 2;
    }

    constexpr size_t activation_bytes = 4 * 1024 * 1024;
    auto main_view = read_file(argv[3], activation_bytes);
    auto skip_view = read_file(argv[4], activation_bytes);
    auto weights = read_file(argv[5], 21808);
    auto blend = read_file(argv[6], 2);
    auto rgba = read_file(argv[7], static_cast<size_t>(width) * height * 16);

    check("cuInit", cuInit(0));
    CUdevice device;
    check("cuDeviceGet", cuDeviceGet(&device, 0));
    CUcontext context;
    check("cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain(&context, device));
    check("cuCtxSetCurrent", cuCtxSetCurrent(context));
    CUmodule module;
    check("cuModuleLoad", cuModuleLoad(&module, cubin_path));
    CUfunction function;
    check("cuModuleGetFunction", cuModuleGetFunction(&function, module, symbol));

    CUdeviceptr main_device, skip_device, weights_device, blend_device;
    check("alloc main", cuMemAlloc(&main_device, main_view.size()));
    check("alloc skip", cuMemAlloc(&skip_device, skip_view.size()));
    check("alloc weights", cuMemAlloc(&weights_device, weights.size()));
    check("alloc blend", cuMemAlloc(&blend_device, 512));
    check("copy main", cuMemcpyHtoD(main_device, main_view.data(), main_view.size()));
    check("copy skip", cuMemcpyHtoD(skip_device, skip_view.data(), skip_view.size()));
    check("copy weights", cuMemcpyHtoD(weights_device, weights.data(), weights.size()));
    check("copy blend", cuMemcpyHtoD(blend_device, blend.data(), blend.size()));

    CUDA_ARRAY3D_DESCRIPTOR output_desc{};
    output_desc.Width = width;
    output_desc.Height = height;
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

    CUDA_ARRAY3D_DESCRIPTOR texture_desc{};
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.Format = CU_AD_FORMAT_FLOAT;
    texture_desc.NumChannels = 4;
    CUarray texture_array;
    check("texture array", cuArray3DCreate(&texture_array, &texture_desc));
    CUDA_MEMCPY2D upload{};
    upload.srcMemoryType = CU_MEMORYTYPE_HOST;
    upload.srcHost = rgba.data();
    upload.srcPitch = width * 16;
    upload.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    upload.dstArray = texture_array;
    upload.WidthInBytes = width * 16;
    upload.Height = height;
    check("texture upload", cuMemcpy2D(&upload));
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
    std::memcpy(params + 0x20, &height, 4);
    std::memcpy(params + 0x24, &width, 4);
    std::memcpy(params + 0x30, &input_scale, 4);
    std::memcpy(params + 0x34, &rgb_mode, 4);
    if (texture_mask & 1) std::memcpy(params + 0x38, &texture, 8);
    if (texture_mask & 2) std::memcpy(params + 0x58, &texture, 8);
    if (texture_mask & 4) std::memcpy(params + 0x60, &texture, 8);
    const float texture_transform[6] = {0.0f, 0.0f, 1.0f,
                                        1.0f, 1.0f, 1.0f};
    std::memcpy(params + 0x40, texture_transform, sizeof(texture_transform));
    std::memcpy(params + 0x68, &blend_device, 8);
    const float one = 1.0f;
    std::memcpy(params + 0xa4, &one, 4);
    std::memcpy(params + 0xa8, &one, 4);
    std::memcpy(params + 0xac, &width, 4);
    std::memcpy(params + 0xb0, &height, 4);

    void *arguments[] = {params};
    check("launch", cuLaunchKernel(function, (width + 7) / 8, (height + 7) / 8,
                                    1, 32, 2, 1, 0, nullptr, arguments, nullptr));
    check("sync", cuCtxSynchronize());

    std::vector<float> output(static_cast<size_t>(width) * height * 4);
    CUDA_MEMCPY2D download{};
    download.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    download.srcArray = output_array;
    download.dstMemoryType = CU_MEMORYTYPE_HOST;
    download.dstHost = output.data();
    download.dstPitch = width * 16;
    download.WidthInBytes = width * 16;
    download.Height = height;
    check("download", cuMemcpy2D(&download));
    std::ofstream stream(output_path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(output.data()),
                 output.size() * sizeof(float));

    size_t finite = 0;
    double chroma = 0.0;
    for (size_t pixel = 0; pixel < output.size(); pixel += 4) {
        finite += std::isfinite(output[pixel]);
        chroma += std::abs(output[pixel] - output[pixel + 1]);
        chroma += std::abs(output[pixel + 1] - output[pixel + 2]);
    }
    std::printf("texture-mask=%d rgb=%d finite=%zu chroma=%.9g "
                "range=%.9g..%.9g output=%s\n",
                texture_mask, rgb_mode, finite, chroma,
                *std::min_element(output.begin(), output.end()),
                *std::max_element(output.begin(), output.end()), output_path);
    return stream ? 0 : 3;
}
