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

static std::vector<unsigned char> read_native(const char*path,size_t capacity,size_t valid){
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)std::exit(2);auto n=f.tellg();
    if(n<=0||size_t(n)>capacity||size_t(n)<valid)std::exit(2);
    std::vector<unsigned char>v(capacity,0);f.seekg(0);if(!f.read((char*)v.data(),n))std::exit(2);
    if(!std::all_of(v.begin()+valid,v.end(),[](unsigned char x){return x==0;}))std::exit(2);
    return v;
}

int main(int argc, char **argv) {
    if (argc < 11 || argc > 15) {
        std::fprintf(stderr,
            "usage: %s cubin symbol main skip weights blend color output "
            "width height [texture-mask=1] [rgb-mode=1] [input-scale=0.03125] "
            "[features|native]\n",
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
    const bool feature_mode = argc > 14 && !std::strcmp(argv[14], "features");
    const bool native_mode = argc > 14 && !std::strcmp(argv[14], "native");
    if (argc > 14 && !feature_mode && !native_mode) return 2;
    if(native_mode&&(width<16||height<16||width>512||height>512||width%16||height%16))return 2;
    if (width <= 0 || height <= 0) {
        std::fprintf(stderr, "invalid dimensions %dx%d\n", width, height);
        return 2;
    }

    const size_t activation_bytes = native_mode?size_t(width)*height*32+65536:320*1024*1024;
    auto main_view = native_mode?read_native(argv[3],activation_bytes,size_t(width)*height*8):read_file(argv[3], activation_bytes);
    auto skip_view = native_mode?read_native(argv[4],activation_bytes,size_t(width)*height*32):read_file(argv[4], activation_bytes);
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
    check("clear blend", cuMemsetD8(blend_device,0,512));
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
    const CUdeviceptr main_pointer = main_device + (native_mode?0:0x2800);
    const CUdeviceptr skip_pointer = skip_device + (native_mode?0:0x2800);
    std::memcpy(params + 0x00, &main_pointer, 8);
    std::memcpy(params + 0x08, &skip_pointer, 8);
    std::memcpy(params + 0x10, &output_surface, 8);
    std::memcpy(params + 0x18, &weights_device, 8);
    std::memcpy(params + 0x20, &height, 4);
    std::memcpy(params + 0x24, &width, 4);
    std::memcpy(params + 0x30, &input_scale, 4);
    std::memcpy(params + 0x34, &rgb_mode, 4);
    if (texture_mask & 1) std::memcpy(params + 0x38, &texture, 8);
    if (texture_mask & 2) std::memcpy(params + 0x58, &texture, 8);
    if (texture_mask & 4) std::memcpy(params + 0x60, &texture, 8);
    const unsigned long long texture_transform[3] = {
        0x0ull, 0x4507000045700000ull, 0x39f2b9d639888889ull};
    std::memcpy(params + 0x40, texture_transform, sizeof(texture_transform));
    std::memcpy(params + 0x68, &blend_device, 8);
    const unsigned long long live_tail[3] = {
        0x3988888900000000ull, 0x00000f0039f2b9d6ull, 0x870ull};
    std::memcpy(params + 0xa0, live_tail, sizeof(live_tail));
    if(native_mode){
        const float transform[]={0,0,float(width),float(height),1.0f/width,1.0f/height};
        std::memcpy(params+0x40,transform,sizeof(transform));
        const float inv_width=1.0f/width,inv_height=1.0f/height;
        std::memcpy(params+0xa4,&inv_width,4);std::memcpy(params+0xa8,&inv_height,4);
        std::memcpy(params+0xac,&width,4);std::memcpy(params+0xb0,&height,4);
    }

    void *arguments[] = {params};
    std::vector<float> output(static_cast<size_t>(width) * height * 4);
    CUDA_MEMCPY2D download{};
    download.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    download.srcArray = output_array;
    download.dstMemoryType = CU_MEMORYTYPE_HOST;
    download.dstHost = output.data();
    download.dstPitch = width * 16;
    download.WidthInBytes = width * 16;
    download.Height = height;
    const auto launch_and_download = [&]() {
        check("launch", cuLaunchKernel(function, (width + 7) / 8 + 1,
            (height + 7) / 8 + 1, 1, 32, 2, 1, 0, nullptr, arguments, nullptr));
        check("sync", cuCtxSynchronize());
        check("download", cuMemcpy2D(&download));
    };
    if (feature_mode) {
        std::vector<unsigned char> controlled(weights.size(), 0);
        std::copy(weights.begin(), weights.begin() + 10392 * 2,
                  controlled.begin());
        std::vector<float> features(static_cast<size_t>(width) * height * 32);
        std::vector<float> positive(static_cast<size_t>(width) * height);
        constexpr unsigned short positive_half = 0x1400; // FP16 1/1024
        constexpr unsigned short negative_half = 0x9400; // FP16 -1/1024
        constexpr unsigned short zero_half = 0;
        constexpr float probe = 1.0f / 1024.0f;
        for (size_t channel = 0; channel < 32; ++channel) {
            const size_t block = channel < 16 ? 10392 : 10648;
            const size_t local = channel & 15;
            const size_t slot = block + (local / 4) * 8 + local % 4;
            std::memcpy(controlled.data() + slot * 2, &positive_half, 2);
            check("controlled weights", cuMemcpyHtoD(
                weights_device, controlled.data(), controlled.size()));
            launch_and_download();
            for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height;
                 ++pixel)
                positive[pixel] = output[pixel * 4];
            std::memcpy(controlled.data() + slot * 2, &negative_half, 2);
            check("controlled weights", cuMemcpyHtoD(
                weights_device, controlled.data(), controlled.size()));
            launch_and_download();
            for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height;
                 ++pixel) {
                const float source =
                    reinterpret_cast<const float *>(rgba.data())[pixel * 4];
                const float from_positive = (positive[pixel] - source) / probe;
                const float from_negative = (source - output[pixel * 4]) / probe;
                features[pixel * 32 + channel] =
                    std::abs(from_positive) >= std::abs(from_negative)
                    ? from_positive : from_negative;
            }
            std::memcpy(controlled.data() + slot * 2, &zero_half, 2);
        }
        std::ofstream stream(output_path, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(features.data()),
                     features.size() * sizeof(float));
        const auto [low, high] = std::minmax_element(features.begin(), features.end());
        size_t finite = 0;
        for (float value : features) finite += std::isfinite(value);
        std::printf("features=%zu finite=%zu range=%.9g..%.9g output=%s\n",
                    features.size(), finite, *low, *high, output_path);
        return stream ? 0 : 3;
    }
    launch_and_download();
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
