#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

struct Params {
    CUdeviceptr input, output, weights;
    int width, height, shift_y, shift_x;
    CUdeviceptr optional0, optional1, optional2, optional3;
    unsigned long long optional_dims;
    CUdeviceptr optional4;
    int override_width, override_height;
};
static_assert(sizeof(Params) == 0x60);

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *text = nullptr; cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s=%d %s\n", name, result, text ? text : "?");
        std::exit(1);
    }
}
static std::vector<unsigned char> read_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) std::exit(2);
    size_t size = static_cast<size_t>(file.tellg()); file.seekg(0);
    std::vector<unsigned char> bytes(size);
    file.read(reinterpret_cast<char *>(bytes.data()), size); return bytes;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s cubin block4.weights input.fp8 output.fp8\n", argv[0]);
        return 2;
    }
    auto weights = read_file(argv[2]), input = read_file(argv[3]);
    if (weights.size() != 22720 || input.empty() || input.size() % 2048) return 2;
    const size_t tiles = input.size() / 2048;
    std::vector<unsigned char> output(tiles * 512);
    check("cuInit", cuInit(0)); CUdevice device;
    check("cuDeviceGet", cuDeviceGet(&device, 0)); CUcontext context;
    check("cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain(&context, device));
    check("cuCtxSetCurrent", cuCtxSetCurrent(context)); CUmodule module;
    check("cuModuleLoad", cuModuleLoad(&module, argv[1])); CUfunction function;
    check("cuModuleGetFunction", cuModuleGetFunction(&function, module,
        "cc_tinlayout_fused_swin_1h_32_1_ds_fp8"));
    CUdeviceptr device_input, device_output, device_weights, auxiliary;
    check("alloc input", cuMemAlloc(&device_input, 2048));
    check("alloc output", cuMemAlloc(&device_output, 8192));
    check("alloc weights", cuMemAlloc(&device_weights, weights.size()));
    check("alloc auxiliary", cuMemAlloc(&auxiliary, 8192));
    check("copy weights", cuMemcpyHtoD(device_weights, weights.data(), weights.size()));
    Params params{}; params.input = device_input; params.output = device_output;
    params.weights = device_weights; params.width = params.height = 8;
    params.shift_y = params.shift_x = -4; params.optional3 = auxiliary;
    params.optional_dims = 8ull | (8ull << 32);
    params.override_width = params.override_height = 4;
    void *kernel_args[] = {&params};
    for (size_t tile = 0; tile < tiles; ++tile) {
        check("copy input", cuMemcpyHtoD(device_input, input.data() + tile * 2048, 2048));
        check("zero output", cuMemsetD8(device_output, 0, 8192));
        check("zero auxiliary", cuMemsetD8(auxiliary, 0, 8192));
        check("launch", cuLaunchKernel(function, 1, 1, 1, 32, 1, 1,
            0, nullptr, kernel_args, nullptr));
        check("sync", cuCtxSynchronize());
        check("copy output", cuMemcpyDtoH(output.data() + tile * 512,
            device_output, 512));
    }
    std::ofstream(argv[4], std::ios::binary).write(
        reinterpret_cast<const char *>(output.data()), output.size());
    size_t nonzero = 0; for (auto value : output) nonzero += value != 0;
    std::printf("tiles=%zu nonzero=%zu/%zu\n", tiles, nonzero, output.size());
}
