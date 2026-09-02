#include <cuda.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *text = nullptr; cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s=%d %s\n", name, result, text ? text : "?");
        std::exit(1);
    }
}
static std::vector<unsigned char> read_file(const char *path, size_t size) {
    std::vector<unsigned char> result(size);
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char *>(result.data()), size)) std::exit(2);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "usage: %s cubin weights output.fp8 basis-count [basis-start]\n", argv[0]);
        return 2;
    }
    const size_t basis_count = std::strtoull(argv[4], nullptr, 0);
    const bool hadamard = argc == 6 && !std::strcmp(argv[5], "hadamard");
    const size_t basis_start = argc == 6 && !hadamard
        ? std::strtoull(argv[5], nullptr, 0) : 0;
    if (!basis_count || basis_start + basis_count > 65536) return 2;
    auto weights = read_file(argv[2], 525312);
    check("init", cuInit(0)); CUdevice device; check("device", cuDeviceGet(&device, 0));
    CUcontext context; check("context", cuDevicePrimaryCtxRetain(&context, device));
    check("current", cuCtxSetCurrent(context)); CUmodule module;
    check("module", cuModuleLoad(&module, argv[1])); CUfunction function;
    check("function", cuModuleGetFunction(&function, module, "cc_dec_input_upsample_1024_512_fp8"));
    constexpr size_t arena = 2 * 1024 * 1024, saved = 8192;
    CUdeviceptr main, skip, scratch, aux0, output, device_weights;
    for (auto pointer : {&main, &skip, &scratch, &aux0, &output}) {
        check("alloc", cuMemAlloc(pointer, arena)); check("zero", cuMemsetD8(*pointer, 0, arena));
    }
    check("weight alloc", cuMemAlloc(&device_weights, weights.size()));
    check("weight upload", cuMemcpyHtoD(device_weights, weights.data(), weights.size()));
    alignas(8) unsigned char params[0x50]{};
    std::memcpy(params + 0x00, &main, 8); std::memcpy(params + 0x08, &skip, 8);
    std::memcpy(params + 0x10, &scratch, 8); std::memcpy(params + 0x20, &aux0, 8);
    std::memcpy(params + 0x30, &output, 8); std::memcpy(params + 0x38, &device_weights, 8);
    const int extent = 32; std::memcpy(params + 0x40, &extent, 4);
    std::memcpy(params + 0x44, &extent, 4);
    const unsigned long long dimensions = 16ull | (16ull << 32);
    std::memcpy(params + 0x48, &dimensions, 8); void *arguments[] = {params};
    std::vector<unsigned char> results(basis_count * saved), tile(saved), dense(8192);
    for (size_t basis = 0; basis < basis_count; ++basis) {
        check("main zero", cuMemsetD8(main, 0, arena));
        check("output zero", cuMemsetD8(output, 0, arena));
        if (hadamard) {
            for (size_t column = 0; column < dense.size(); ++column)
                dense[column] = __builtin_parityll(basis & column) ? 0xb8 : 0x38;
            check("hadamard upload", cuMemcpyHtoD(main, dense.data(), dense.size()));
        } else {
            const unsigned char one = 0x30;
            check("basis upload", cuMemcpyHtoD(main + basis_start + basis, &one, 1));
        }
        check("launch", cuLaunchKernel(function, 4, 4, 2, 32, 2, 1,
                                        0, nullptr, arguments, nullptr));
        check("sync", cuCtxSynchronize());
        check("download", cuMemcpyDtoH(tile.data(), output, saved));
        std::copy(tile.begin(), tile.end(), results.begin() + basis * saved);
    }
    std::ofstream(argv[3], std::ios::binary).write(
        reinterpret_cast<const char *>(results.data()), results.size());
    size_t nonzero = 0; for (auto value : results) nonzero += value != 0;
    std::printf("basis_start=%zu basis=%zu hadamard=%d bytes=%zu nonzero=%zu\n",
                basis_start, basis_count, hadamard, results.size(), nonzero);
}
