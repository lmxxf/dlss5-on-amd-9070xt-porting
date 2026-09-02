#include <cuda.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

struct Params {
    CUdeviceptr main, output, weights, skip;
    uint64_t dimensions;
    uint64_t field;
    CUdeviceptr optional0, optional1, auxiliary, optional3, optional4;
};
static_assert(sizeof(Params) == 0x58);

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *text = nullptr;
        cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s=%d %s\n", name, result, text ? text : "?");
        std::exit(1);
    }
}
static std::vector<uint8_t> read_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) std::exit(2);
    size_t size = static_cast<size_t>(file.tellg()); file.seekg(0);
    std::vector<uint8_t> value(size);
    file.read(reinterpret_cast<char *>(value.data()), size);
    return value;
}

int main(int argc, char **argv) {
    if (argc != 13 && argc != 14) {
        std::fprintf(stderr,
            "usage: %s cubin symbol arena weight-offset main skip aux output width height gx gy [output-init-byte]\n",
            argv[0]);
        return 2;
    }
    auto arena = read_file(argv[3]);
    size_t weight_offset = std::strtoull(argv[4], nullptr, 0);
    auto host_main = read_file(argv[5]);
    auto host_skip = read_file(argv[6]);
    auto host_aux = read_file(argv[7]);
    int width = std::atoi(argv[9]), height = std::atoi(argv[10]);
    int gx = std::atoi(argv[11]), gy = std::atoi(argv[12]);
    const uint8_t output_init = argc == 14
        ? static_cast<uint8_t>(std::strtoul(argv[13], nullptr, 0)) : 0;
    // The 8H kernel always launches eight warps in Y.
    constexpr unsigned block_y = 8;
    constexpr size_t output_bytes = 64 * 1024 * 1024;
    constexpr size_t view_offset = 0x2800;

    check("init", cuInit(0));
    CUdevice device; check("device", cuDeviceGet(&device, 0));
    CUcontext context; check("context", cuDevicePrimaryCtxRetain(&context, device));
    check("current", cuCtxSetCurrent(context));
    CUmodule module; check("module", cuModuleLoad(&module, argv[1]));
    CUfunction function; check("function", cuModuleGetFunction(&function, module, argv[2]));
    CUdeviceptr device_arena, main_resource, skip_resource, aux_resource, output_resource;
    check("arena alloc", cuMemAlloc(&device_arena, arena.size()));
    check("main alloc", cuMemAlloc(&main_resource, host_main.size()));
    check("skip alloc", cuMemAlloc(&skip_resource, host_skip.size()));
    check("aux alloc", cuMemAlloc(&aux_resource, host_aux.size()));
    check("output alloc", cuMemAlloc(&output_resource, output_bytes));
    check("arena upload", cuMemcpyHtoD(device_arena, arena.data(), arena.size()));
    check("main upload", cuMemcpyHtoD(main_resource, host_main.data(), host_main.size()));
    check("skip upload", cuMemcpyHtoD(skip_resource, host_skip.data(), host_skip.size()));
    check("aux upload", cuMemcpyHtoD(aux_resource, host_aux.data(), host_aux.size()));
    check("output init", cuMemsetD8(output_resource, output_init, output_bytes));
    Params params{};
    params.main = main_resource + view_offset;
    params.output = output_resource + view_offset;
    params.weights = device_arena + weight_offset;
    params.skip = skip_resource + view_offset;
    params.dimensions = static_cast<uint32_t>(width) |
        (static_cast<uint64_t>(static_cast<uint32_t>(height)) << 32);
    params.auxiliary = aux_resource;
    void *arguments[] = {&params};
    check("launch", cuLaunchKernel(
        function, gx, gy, 1, 32, block_y, 1, 0, nullptr, arguments, nullptr));
    check("sync", cuCtxSynchronize());
    std::vector<uint8_t> output(output_bytes);
    check("read", cuMemcpyDtoH(output.data(), output_resource, output.size()));
    std::ofstream(argv[8], std::ios::binary).write(
        reinterpret_cast<const char *>(output.data()), output.size());
    size_t nonzero = 0, last = 0;
    for (size_t index = 0; index < output.size(); ++index) {
        if (output[index]) { ++nonzero; last = index; }
    }
    std::printf("nonzero=%zu last=%zu bytes=%zu\n", nonzero, last, output.size());
}
