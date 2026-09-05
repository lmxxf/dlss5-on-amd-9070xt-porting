#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *text = nullptr;
        cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s=%d %s\n", name, result, text ? text : "?");
        std::exit(1);
    }
}

static std::vector<unsigned char> read_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) std::exit(2);
    const size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<unsigned char> bytes(size);
    file.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}

static void write_file(const char *path, const void *data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    if (!file) std::exit(2);
    file.write(static_cast<const char *>(data), size);
}

int main(int argc, char **argv) {
    if (argc < 6 || argc > 9) {
        std::fprintf(stderr,
            "usage: %s cubin block0.weights input-8x8-rgba32f main.fp8 ds.fp8 [texture-slot 0..3] [gaussian-scale]\n",
            argv[0]);
        return 2;
    }
    const int texture_slot = argc >= 7 ? std::atoi(argv[6]) : 3;
    const float gaussian_scale = argc >= 8 ? std::strtof(argv[7], nullptr) : 0.0f;
    const bool adapter_scan=argc==9 && !std::strcmp(argv[8],"adapter-scan");
    if(argc==9&&!adapter_scan)return 2;
    if (texture_slot < 0 || texture_slot > 3) return 2;
    const auto weights = read_file(argv[2]);
    const auto input = read_file(argv[3]);
    constexpr size_t input_tile_bytes = 8 * 8 * 4 * sizeof(float);
    if (weights.size() != 21696 || input.empty() || input.size() % input_tile_bytes) return 2;
    if(adapter_scan&&input.size()!=input_tile_bytes)return 2;
    const size_t tile_count = adapter_scan?224:input.size() / input_tile_bytes;

    check("cuInit", cuInit(0));
    CUdevice device;
    check("cuDeviceGet", cuDeviceGet(&device, 0));
    CUcontext context;
    check("cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain(&context, device));
    check("cuCtxSetCurrent", cuCtxSetCurrent(context));
    CUmodule module;
    check("cuModuleLoad", cuModuleLoad(&module, argv[1]));
    CUfunction function;
    check("cuModuleGetFunction", cuModuleGetFunction(&function, module,
        "cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8"));

    CUdeviceptr device_weights, main_output, downsample_output;
    check("cuMemAlloc(weights)", cuMemAlloc(&device_weights, weights.size()));
    check("cuMemcpyHtoD(weights)",
        cuMemcpyHtoD(device_weights, weights.data(), weights.size()));
    check("cuMemAlloc(main)", cuMemAlloc(&main_output, 1 << 20));
    check("cuMemAlloc(downsample)", cuMemAlloc(&downsample_output, 1 << 20));
    check("cuMemsetD8(main)", cuMemsetD8(main_output, 0, 1 << 20));
    check("cuMemsetD8(downsample)", cuMemsetD8(downsample_output, 0, 1 << 20));

    CUDA_ARRAY3D_DESCRIPTOR array_desc{};
    array_desc.Width = 8;
    array_desc.Height = 8;
    array_desc.Format = CU_AD_FORMAT_FLOAT;
    array_desc.NumChannels = 4;
    CUarray array;
    check("cuArray3DCreate", cuArray3DCreate(&array, &array_desc));
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = input.data();
    copy.srcPitch = 8 * 4 * sizeof(float);
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = array;
    copy.WidthInBytes = copy.srcPitch;
    copy.Height = 8;
    check("cuMemcpy2D", cuMemcpy2D(&copy));

    CUDA_RESOURCE_DESC resource_desc{};
    resource_desc.resType = CU_RESOURCE_TYPE_ARRAY;
    resource_desc.res.array.hArray = array;
    CUDA_TEXTURE_DESC texture_desc{};
    texture_desc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texture_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;
    CUtexObject texture;
    check("cuTexObjectCreate", cuTexObjectCreate(
        &texture, &resource_desc, &texture_desc, nullptr));

    // Kernel metadata: one by-value parameter object, size 0x108, cbank start 0x380.
    alignas(8) unsigned char params[0x108]{};
    for (int offset = 0x48; offset < 0xd8; offset += 4) {
        const float one = 1.0f;
        std::memcpy(params + offset, &one, sizeof(one));
    }
    const int extent = 8;
    for (int offset : {0x78, 0xd0, 0xd4})
        std::memcpy(params + offset, &extent, sizeof(extent));
    const unsigned zero = 0;
    const unsigned gaussian_enabled = gaussian_scale != 0.0f;
    std::memcpy(params + 0x88, &zero, sizeof(zero));
    std::memcpy(params + 0x8c, &zero, sizeof(zero));
    std::memcpy(params + 0xb0, &gaussian_enabled, sizeof(gaussian_enabled));
    std::memcpy(params + 0xb4, &gaussian_scale, sizeof(gaussian_scale));
    std::memcpy(params + 0xc0, &zero, sizeof(zero));
    const unsigned long long dimensions = 8ull | (8ull << 32);
    const int texture_offsets[] = {0x00, 0x08, 0x18, 0x20};
    std::memcpy(params + texture_offsets[texture_slot], &texture, 8);
    std::memcpy(params + 0xd8, &main_output, 8);
    std::memcpy(params + 0xe0, &device_weights, 8); // tensor-core weight view
    std::memcpy(params + 0xf0, &dimensions, 8);
    std::memcpy(params + 0xf8, &downsample_output, 8);

    void *kernel_args[] = {params};
    constexpr size_t main_tile_bytes = 8 * 8 * 32;
    constexpr size_t downsample_tile_bytes = 4 * 4 * 32;
    std::vector<unsigned char> main_bytes(tile_count * main_tile_bytes);
    std::vector<unsigned char> downsample_bytes(tile_count * downsample_tile_bytes);
    for (size_t tile = 0; tile < tile_count; ++tile) {
        if(adapter_scan){
            auto probe_weights=weights;
            std::memset(probe_weights.data(),0,224*2);
            const unsigned short one_half=0x3c00;
            std::memcpy(probe_weights.data()+tile*2,&one_half,2);
            check("adapter probe upload",cuMemcpyHtoD(device_weights,probe_weights.data(),probe_weights.size()));
        }
        copy.srcHost = input.data() + (adapter_scan?0:tile) * input_tile_bytes;
        check("cuMemcpy2D(tile)", cuMemcpy2D(&copy));
        check("cuMemsetD8(main)", cuMemsetD8(main_output, 0, 1 << 20));
        check("cuMemsetD8(downsample)", cuMemsetD8(downsample_output, 0, 1 << 20));
        check("cuLaunchKernel", cuLaunchKernel(
            function, 1, 1, 1, 32, 2, 1, 0, nullptr, kernel_args, nullptr));
        check("cuCtxSynchronize", cuCtxSynchronize());
        check("cuMemcpyDtoH(main)", cuMemcpyDtoH(
            main_bytes.data() + tile * main_tile_bytes, main_output, main_tile_bytes));
        check("cuMemcpyDtoH(downsample)", cuMemcpyDtoH(
            downsample_bytes.data() + tile * downsample_tile_bytes,
            downsample_output, downsample_tile_bytes));
    }
    write_file(argv[4], main_bytes.data(), main_bytes.size());
    write_file(argv[5], downsample_bytes.data(), downsample_bytes.size());
    size_t main_nonzero = 0, downsample_nonzero = 0;
    for (auto value : main_bytes) main_nonzero += value != 0;
    for (auto value : downsample_bytes) downsample_nonzero += value != 0;
    std::printf("tiles=%zu main_nonzero=%zu/%zu downsample_nonzero=%zu/%zu\n",
        tile_count, main_nonzero, main_bytes.size(), downsample_nonzero,
        downsample_bytes.size());
}
