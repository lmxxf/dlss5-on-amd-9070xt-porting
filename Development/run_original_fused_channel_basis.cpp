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
    if (argc != 7) {
        std::fprintf(stderr,
            "usage: %s cubin symbol controlled.weights canonical-to-physical.i32 output.bin amplitude-byte\n",
            argv[0]);
        return 2;
    }
    constexpr size_t arena_bytes = 4 * 1024 * 1024;
    constexpr size_t physical_bytes = 8 * 8 * 256;
    constexpr unsigned channels = 256;
    auto weights = read_file(argv[3]);
    auto mapping_bytes = read_file(argv[4]);
    if (mapping_bytes.size() != physical_bytes * sizeof(uint32_t)) return 2;
    const auto *mapping = reinterpret_cast<const uint32_t *>(mapping_bytes.data());
    const uint8_t amplitude = static_cast<uint8_t>(std::strtoul(argv[6], nullptr, 0));

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
    params.height = params.width = 8;
    params.auxiliary = auxiliary;
    params.auxiliary_800 = auxiliary + 0x800;
    void *arguments[] = {&params};

    std::vector<uint8_t> host_input(arena_bytes), host_output(physical_bytes);
    std::ofstream output_file(argv[5], std::ios::binary);
    if (!output_file) return 2;
    for (unsigned basis = 0; basis < channels; ++basis) {
        std::fill(host_input.begin(), host_input.end(), 0);
        host_input[mapping[basis]] = amplitude; // canonical token 0, channel=basis
        check("input", cuMemcpyHtoD(input, host_input.data(), arena_bytes));
        check("output zero", cuMemsetD8(output, 0, arena_bytes));
        check("launch", cuLaunchKernel(
            function, 1, 1, 1, 32, 8, 1, 0, nullptr, arguments, nullptr));
        check("sync", cuCtxSynchronize());
        check("output", cuMemcpyDtoH(host_output.data(), output, physical_bytes));
        output_file.write(reinterpret_cast<const char *>(host_output.data()), host_output.size());
    }
    std::printf("basis=%u record_bytes=%zu output_bytes=%zu\n",
        channels, physical_bytes, channels * physical_bytes);
}
