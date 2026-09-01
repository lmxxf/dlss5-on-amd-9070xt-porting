#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, CUresult result) {
  if (result == CUDA_SUCCESS)
    return;
  const char *message = nullptr;
  cuGetErrorString(result, &message);
  std::fprintf(stderr, "%s=%d %s\n", name, result,
               message ? message : "unknown");
  std::exit(1);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s input-1d output-2d\n", argv[0]);
    return 2;
  }
  constexpr size_t bytes = 2 * 1024 * 1024;
  std::vector<unsigned char> input(bytes), output(bytes);
  std::ifstream(argv[1], std::ios::binary).read(
      reinterpret_cast<char *>(input.data()), input.size());
  check("init", cuInit(0));
  CUdevice device;
  check("device", cuDeviceGet(&device, 0));
  CUcontext context;
  check("context", cuDevicePrimaryCtxRetain(&context, device));
  check("set", cuCtxSetCurrent(context));
  CUmodule module;
  check("module",
        cuModuleLoad(&module, "/tmp/dlssnr-cubins/dlssnr-05.cubin"));
  CUfunction repack;
  check("function", cuModuleGetFunction(
                        &repack, module, "cc_vit_1d_repack_1d_to_2d_fp8"));
  CUdeviceptr source, destination;
  check("source", cuMemAlloc(&source, bytes));
  check("destination", cuMemAlloc(&destination, bytes));
  check("upload", cuMemcpyHtoD(source, input.data(), bytes));
  check("clear", cuMemsetD8(destination, 0, bytes));
  int width = 8, height = 8;
  unsigned char params[0x18]{};
  std::memcpy(params + 0, &source, 8);
  std::memcpy(params + 8, &destination, 8);
  std::memcpy(params + 16, &width, 4);
  std::memcpy(params + 20, &height, 4);
  void *args[] = {params};
  check("launch", cuLaunchKernel(repack, 64, 1, 1, 32, 4, 1, 0, nullptr,
                                  args, nullptr));
  check("sync", cuCtxSynchronize());
  check("download", cuMemcpyDtoH(output.data(), destination, bytes));
  std::ofstream(argv[2], std::ios::binary).write(
      reinterpret_cast<const char *>(output.data()), output.size());
  size_t nonzero = 0, nan = 0, last = 0;
  for (size_t i = 0; i < output.size(); ++i) {
    nonzero += output[i] != 0;
    nan += output[i] == 0x7f || output[i] == 0xff;
    if (output[i])
      last = i;
  }
  std::printf("nonzero=%zu last=%zu nan=%zu\n", nonzero, last, nan);
  return nan ? 3 : 0;
}
