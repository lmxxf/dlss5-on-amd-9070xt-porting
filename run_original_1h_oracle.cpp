#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

struct Params {
    CUdeviceptr input;
    CUdeviceptr output;
    CUdeviceptr weights;
    int width;
    int height;
    int shift_y;
    int shift_x;
    CUdeviceptr optional0;
    CUdeviceptr optional1;
    CUdeviceptr optional2;
    CUdeviceptr optional3;
    unsigned long long optional_dims;
    CUdeviceptr optional4;
    int override_width;
    int override_height;
};
static_assert(sizeof(Params) == 0x60);

static void ck(const char *name, CUresult r) {
    if (r != CUDA_SUCCESS) {
        const char *s = nullptr; cuGetErrorString(r, &s);
        std::fprintf(stderr, "%s=%d %s\n", name, r, s ? s : "?");
        std::exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <cubin> <flat-weights> <kernel-symbol>\n", argv[0]);
        return 2;
    }
    std::ifstream wf(argv[2], std::ios::binary | std::ios::ate);
    size_t weight_size = wf.tellg(); wf.seekg(0);
    std::vector<unsigned char> weights(weight_size); wf.read((char*)weights.data(), weight_size);
    constexpr size_t buffer_size = 1024 * 1024;
    std::vector<unsigned char> input(buffer_size), output(buffer_size);
    ck("cuInit", cuInit(0));
    CUdevice device; ck("cuDeviceGet", cuDeviceGet(&device, 0));
    CUcontext context; ck("cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain(&context, device));
    ck("cuCtxSetCurrent", cuCtxSetCurrent(context));
    CUmodule module; ck("cuModuleLoad", cuModuleLoad(&module, argv[1]));
    CUfunction function; ck("cuModuleGetFunction", cuModuleGetFunction(&function, module, argv[3]));
    CUdeviceptr di, dout, dw;
    ck("cuMemAlloc input", cuMemAlloc(&di, buffer_size));
    ck("cuMemAlloc output", cuMemAlloc(&dout, buffer_size));
    ck("cuMemAlloc weights", cuMemAlloc(&dw, weight_size));
    ck("copy weights", cuMemcpyHtoD(dw, weights.data(), weights.size()));
    Params params{}; params.input=di; params.output=dout; params.weights=dw; params.width=8; params.height=8;
    void *kernel_args[] = {&params};
    std::puts("basis,nonzero,output,value");
    for (unsigned basis = 0; basis < 2048; ++basis) {
        std::memset(input.data(), 0, input.size());
        input[basis] = 0x38; // E4M3 1.0
        ck("copy input", cuMemcpyHtoD(di, input.data(), 2048));
        ck("zero output", cuMemsetD8(dout, 0, buffer_size));
        ck("cuLaunchKernel", cuLaunchKernel(function, 1, 1, 1, 32, 1, 1, 0, nullptr, kernel_args, nullptr));
        ck("cuCtxSynchronize", cuCtxSynchronize());
        ck("copy output", cuMemcpyDtoH(output.data(), dout, 2048));
        unsigned count = 0, first = 0; unsigned char value = 0;
        for (unsigned i = 0; i < 2048; ++i) if (output[i] != 0) { if (count++ == 0) { first=i; value=output[i]; } }
        std::printf("%u,%u,%u,%u\n", basis, count, first, value);
    }
    std::fprintf(stderr, "weight_size=%zu basis_count=2048\n", weight_size);
}
