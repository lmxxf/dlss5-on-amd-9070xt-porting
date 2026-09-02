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
static std::vector<unsigned char> read_file(const char *path, size_t size) {
  std::vector<unsigned char> result(size);
  std::ifstream file(path, std::ios::binary);
  if (!file || !file.read(reinterpret_cast<char *>(result.data()), size))
    std::exit(2);
  return result;
}
int main(int argc, char **argv) {
  if (argc != 5) {
    std::fprintf(stderr,
                 "usage: %s weight-arena weight-offset input output\n",
                 argv[0]);
    return 2;
  }
  constexpr size_t bytes = 2 * 1024 * 1024;
  constexpr size_t weightBytes = 147719680;
  auto weights = read_file(argv[1], weightBytes);
  auto hostInput = read_file(argv[3], bytes);
  check("init", cuInit(0));
  CUdevice device;
  check("device", cuDeviceGet(&device, 0));
  CUcontext context;
  check("context", cuDevicePrimaryCtxRetain(&context, device));
  check("set", cuCtxSetCurrent(context));
  CUmodule module;
  check("module",
        cuModuleLoad(&module, "/tmp/dlssnr-cubins/dlssnr-05.cubin"));
  CUfunction function;
  check("function", cuModuleGetFunction(&function, module,
                                         "cc_vit_1d_ffn_expand_fp8"));
  CUdeviceptr input, output, auxiliary, deviceWeights;
  for (CUdeviceptr *pointer : {&input, &output, &auxiliary}) {
    check("alloc", cuMemAlloc(pointer, bytes));
    check("zero", cuMemsetD8(*pointer, 0, bytes));
  }
  check("weight_alloc", cuMemAlloc(&deviceWeights, weightBytes));
  check("input", cuMemcpyHtoD(input, hostInput.data(), bytes));
  check("weights", cuMemcpyHtoD(deviceWeights, weights.data(), weightBytes));
  uint64_t tokens = 64;
  unsigned char params[0x48]{};
  std::memcpy(params + 0, &input, 8);
  std::memcpy(params + 16, &output, 8);
  CUdeviceptr blockWeights = deviceWeights + std::strtoull(argv[2], nullptr, 0);
  std::memcpy(params + 24, &blockWeights, 8);
  std::memcpy(params + 56, &auxiliary, 8);
  std::memcpy(params + 64, &tokens, 8);
  void *arguments[] = {params};
  check("launch", cuLaunchKernel(function, 32, 1, 1, 32, 4, 1, 0,
                                  nullptr, arguments, nullptr));
  check("sync", cuCtxSynchronize());
  std::vector<unsigned char> hostOutput(bytes);
  check("read", cuMemcpyDtoH(hostOutput.data(), output, bytes));
  std::ofstream(argv[4], std::ios::binary).write(
      reinterpret_cast<const char *>(hostOutput.data()), hostOutput.size());
  return 0;
}
