#include <cuda.h>
#include <algorithm>
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
    struct Scan {const char*name;size_t begin,count,element;unsigned short one;};
    const Scan scans[]={
        {"adapter-scan",8208,224,2,0x3c00},{"prefix-scan",8208,512,2,0x3c00},
        {"ffn1-scan",0,2048,2,0x3c00},{"ffn2-scan",4096,2048,2,0x3c00},
        {"ffn1-byte-scan",0,4096,1,0x38},{"ffn2-byte-scan",4096,4096,1,0x38},
        {"ffn-skip-scan",9232,32,2,0x3c00},
        {"attention-skip-scan",21616,32,2,0x3c00},
        {"v-byte-scan",11360,1024,1,0x38},{"projection-byte-scan",20592,1024,1,0x38},
        {"bias-scan",12384,4096,2,0x4800}
    };
    const Scan*scan=nullptr;
    if(argc==9){for(const auto&s:scans)if(!std::strcmp(argv[8],s.name))scan=&s;if(!scan)return 2;}
    if (texture_slot < 0 || texture_slot > 3) return 2;
    const auto weights = read_file(argv[2]);
    const auto input = read_file(argv[3]);
    const int width=std::getenv("DLSS5_PREBLOCK_WIDTH")?std::atoi(std::getenv("DLSS5_PREBLOCK_WIDTH")):8;
    const int height=std::getenv("DLSS5_PREBLOCK_HEIGHT")?std::atoi(std::getenv("DLSS5_PREBLOCK_HEIGHT")):8;
    if(width<=0||height<=0||width%8||height%8||((width>512||height>512)&&!(width==1920&&height==1152)))return 2;
    if(scan&&(width!=8||height!=8))return 2;
    const size_t input_tile_bytes = size_t(width)*height*4*sizeof(float);
    const size_t allocation_bytes=std::max<size_t>(1<<20,size_t(width)*height*32*4);
    if (weights.size() != 21696 || input.empty() || input.size() % input_tile_bytes) return 2;
    if(scan&&input.size()!=input_tile_bytes)return 2;
    const size_t tile_count = scan?scan->count:input.size() / input_tile_bytes;

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
    check("cuMemAlloc(main)", cuMemAlloc(&main_output, allocation_bytes));
    check("cuMemAlloc(downsample)", cuMemAlloc(&downsample_output, allocation_bytes));
    check("cuMemsetD8(main)", cuMemsetD8(main_output, 0, allocation_bytes));
    check("cuMemsetD8(downsample)", cuMemsetD8(downsample_output, 0, allocation_bytes));

    CUDA_ARRAY3D_DESCRIPTOR array_desc{};
    array_desc.Width = width;
    array_desc.Height = height;
    array_desc.Format = CU_AD_FORMAT_FLOAT;
    array_desc.NumChannels = 4;
    CUarray array;
    check("cuArray3DCreate", cuArray3DCreate(&array, &array_desc));
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = input.data();
    copy.srcPitch = width * 4 * sizeof(float);
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = array;
    copy.WidthInBytes = copy.srcPitch;
    copy.Height = height;
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
    // The legacy b0/b4 "Gaussian" labels do not disable the built-in random
    // fields. SASS reads the integer random seed separately at parameter c8.
    if(const char*seed_text=std::getenv("DLSS5_PREBLOCK_SEED")){
        const uint32_t seed=static_cast<uint32_t>(std::strtoul(seed_text,nullptr,0));
        std::memcpy(params+0xc8,&seed,sizeof(seed));
    }
    std::memcpy(params+0xd0,&height,4);std::memcpy(params+0xd4,&width,4);
    const unsigned long long dimensions = uint64_t(height) | (uint64_t(width) << 32);
    const int texture_offsets[] = {0x00, 0x08, 0x18, 0x20};
    std::memcpy(params + texture_offsets[texture_slot], &texture, 8);
    std::memcpy(params + 0xd8, &main_output, 8);
    std::memcpy(params + 0xe0, &device_weights, 8); // tensor-core weight view
    std::memcpy(params + 0xf0, &dimensions, 8);
    std::memcpy(params + 0xf8, &downsample_output, 8);

    if(const char*parameter_file=std::getenv("DLSS5_PREBLOCK_PARAMETER_FILE")){
        const auto captured=read_file(parameter_file);if(captured.size()!=sizeof(params))return 2;
        std::memcpy(params,captured.data(),sizeof(params));
        // Replace all captured texture/resource handles; retain scalar behavior.
        std::memset(params,0,0x48);
        std::memcpy(params+texture_offsets[texture_slot],&texture,8);
        const float fw=float(width),fh=float(height),iw=1.0f/width,ih=1.0f/height;
        const uint64_t down_dims=uint64_t(height/2)|(uint64_t(width/2)<<32);
        std::memcpy(params+0x90,&fw,4);std::memcpy(params+0x94,&fh,4);
        for(int offset:{0x98,0xa0})std::memcpy(params+offset,&iw,4);
        for(int offset:{0x9c,0xa4})std::memcpy(params+offset,&ih,4);
        std::memcpy(params+0xd0,&height,4);std::memcpy(params+0xd4,&width,4);
        std::memcpy(params+0xd8,&main_output,8);std::memcpy(params+0xe0,&device_weights,8);
        std::memset(params+0xe8,0,8);std::memcpy(params+0xf0,&dimensions,8);
        std::memcpy(params+0xf8,&downsample_output,8);std::memcpy(params+0x100,&down_dims,8);
        if(const char*seed_text=std::getenv("DLSS5_PREBLOCK_SEED")){
            const uint32_t seed=static_cast<uint32_t>(std::strtoul(seed_text,nullptr,0));std::memcpy(params+0xc8,&seed,4);
        }
    }

    void *kernel_args[] = {params};
    const size_t main_tile_bytes = size_t(width)*height*32;
    const size_t downsample_tile_bytes = main_tile_bytes/4;
    std::vector<unsigned char> main_bytes(tile_count * main_tile_bytes);
    std::vector<unsigned char> downsample_bytes(tile_count * downsample_tile_bytes);
    for (size_t tile = 0; tile < tile_count; ++tile) {
        if(scan){
            auto probe_weights=weights;
            std::memset(probe_weights.data()+scan->begin,0,scan->count*scan->element);
            std::memcpy(probe_weights.data()+scan->begin+tile*scan->element,&scan->one,scan->element);
            check("adapter probe upload",cuMemcpyHtoD(device_weights,probe_weights.data(),probe_weights.size()));
        }
        copy.srcHost = input.data() + (scan?0:tile) * input_tile_bytes;
        check("cuMemcpy2D(tile)", cuMemcpy2D(&copy));
        check("cuMemsetD8(main)", cuMemsetD8(main_output, 0, allocation_bytes));
        check("cuMemsetD8(downsample)", cuMemsetD8(downsample_output, 0, allocation_bytes));
        check("cuLaunchKernel", cuLaunchKernel(
            function, width/8, height/8, 1, 32, 2, 1, 0, nullptr, kernel_args, nullptr));
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
