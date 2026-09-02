#include <cuda.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

struct Params {
    CUdeviceptr input, output, weights, skip;
    int height, width, shift_y, shift_x;
    CUdeviceptr auxiliary, optional1, auxiliary_800, optional3, optional4;
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
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> value(size);
    file.read(reinterpret_cast<char *>(value.data()), size);
    return value;
}

int main(int argc, char **argv) {
    if (argc != 12) {
        std::fprintf(stderr,
            "usage: %s cubin symbol identity.weights mapping.i32 height width gx gy block-y shift output-bytes\n",
            argv[0]);
        return 2;
    }
    constexpr size_t arena_bytes = 4 * 1024 * 1024;
    auto weights = read_file(argv[3]);
    int height = std::atoi(argv[5]), width = std::atoi(argv[6]);
    int gx = std::atoi(argv[7]), gy = std::atoi(argv[8]);
    int block_y = std::atoi(argv[9]), shifted = std::atoi(argv[10]);
    size_t output_bytes = std::strtoull(argv[11], nullptr, 0);
    if (output_bytes == 0 || output_bytes > arena_bytes) return 2;

    check("init", cuInit(0));
    CUdevice device; check("device", cuDeviceGet(&device, 0));
    CUcontext context; check("context", cuDevicePrimaryCtxRetain(&context, device));
    check("current", cuCtxSetCurrent(context));
    CUmodule module; check("module", cuModuleLoad(&module, argv[1]));
    CUfunction function; check("function", cuModuleGetFunction(&function, module, argv[2]));
    CUdeviceptr input, output, device_weights, auxiliary;
    for (CUdeviceptr *pointer : {&input, &output, &auxiliary}) {
        check("alloc", cuMemAlloc(pointer, arena_bytes));
        check("zero", cuMemsetD8(*pointer, 0, arena_bytes));
    }
    check("weight alloc", cuMemAlloc(&device_weights, weights.size()));
    check("weight upload", cuMemcpyHtoD(device_weights, weights.data(), weights.size()));
    Params params{};
    params.input = input;
    params.output = output;
    params.weights = device_weights;
    params.height = height;
    params.width = width;
    params.shift_y = params.shift_x = shifted ? -4 : 0;
    params.auxiliary = auxiliary;
    params.auxiliary_800 = auxiliary + 0x800;
    void *arguments[] = {&params};

    std::vector<uint8_t> host_input(arena_bytes), host_output(output_bytes);
    std::vector<uint32_t> mapping(output_bytes);
    for (unsigned bit = 0; bit < 22; ++bit) {
        for (size_t address = 0; address < arena_bytes; ++address)
            host_input[address] = ((address >> bit) & 1) ? 0x38 : 0;
        check("input", cuMemcpyHtoD(input, host_input.data(), arena_bytes));
        check("output zero", cuMemsetD8(output, 0, arena_bytes));
        check("launch", cuLaunchKernel(
            function, gx, gy, 1, 32, block_y, 1, 0, nullptr, arguments, nullptr));
        check("sync", cuCtxSynchronize());
        check("output", cuMemcpyDtoH(host_output.data(), output, output_bytes));
        for (size_t index = 0; index < output_bytes; ++index) {
            if (host_output[index] != 0) mapping[index] |= 1u << bit;
        }
    }
    std::ofstream(argv[4], std::ios::binary).write(
        reinterpret_cast<const char *>(mapping.data()), mapping.size() * sizeof(uint32_t));
    size_t unique = 0;
    std::vector<uint8_t> seen(arena_bytes);
    uint32_t minimum = UINT32_MAX, maximum = 0;
    for (uint32_t address : mapping) {
        if (address < arena_bytes && !seen[address]) { seen[address] = 1; ++unique; }
        if (address < minimum) minimum = address;
        if (address > maximum) maximum = address;
    }
    std::printf("outputs=%zu unique=%zu source_range=%u..%u\n",
        mapping.size(), unique, minimum, maximum);
    return unique == mapping.size() ? 0 : 3;
}
