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
  if (argc != 8) {
    std::fprintf(stderr, "usage: %s arena weight-offset branch residual "
                         "output work-output auxiliary-output\n", argv[0]);
    return 2;
  }
  constexpr size_t bytes = 2 * 1024 * 1024, weightBytes = 147719680;
  auto weights = read_file(argv[1], weightBytes);
  auto hostBranch = read_file(argv[3], bytes);
  auto hostResidual = read_file(argv[4], bytes);
  check("init", cuInit(0));
  CUdevice device;
  check("device", cuDeviceGet(&device, 0));
  CUcontext context;
  check("context", cuDevicePrimaryCtxRetain(&context, device));
  check("set", cuCtxSetCurrent(context));
  CUmodule module;
  check("module", cuModuleLoad(&module, "/tmp/dlssnr-cubins/dlssnr-05.cubin"));
  CUfunction function;
  check("function", cuModuleGetFunction(&function, module,
                                         "cc_vit_1d_ffn_contract_fp8"));
  CUdeviceptr branch, residual, output, work, auxiliary, deviceWeights;
  for (CUdeviceptr *pointer :
       {&branch, &residual, &output, &work, &auxiliary}) {
    check("alloc", cuMemAlloc(pointer, bytes));
    check("zero", cuMemsetD8(*pointer, 0, bytes));
  }
  check("weights_alloc", cuMemAlloc(&deviceWeights, weightBytes));
  check("weights", cuMemcpyHtoD(deviceWeights, weights.data(), weightBytes));
  check("branch", cuMemcpyHtoD(branch, hostBranch.data(), bytes));
  check("residual", cuMemcpyHtoD(residual, hostResidual.data(), bytes));
  CUdeviceptr a1 = auxiliary + 0xa00, a2 = auxiliary + 0xe00;
  uint64_t tokens = 64;
  unsigned char params[0x48]{};
  std::memcpy(params + 0, &branch, 8);
  std::memcpy(params + 8, &residual, 8);
  std::memcpy(params + 16, &output, 8);
  CUdeviceptr blockWeights =
      deviceWeights + std::strtoull(argv[2], nullptr, 0);
  std::memcpy(params + 24, &blockWeights, 8);
  std::memcpy(params + 32, &a1, 8);
  std::memcpy(params + 40, &work, 8);
  std::memcpy(params + 48, &auxiliary, 8);
  std::memcpy(params + 56, &a2, 8);
  std::memcpy(params + 64, &tokens, 8);
  void *arguments[] = {params};
  CUlaunchAttribute attribute{};
  attribute.id = CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;
  attribute.value.clusterDim = {1, 1, 4};
  CUlaunchConfig config{};
  config.gridDimX = 8;
  config.gridDimY = 1;
  config.gridDimZ = 4;
  config.blockDimX = 32;
  config.blockDimY = 4;
  config.blockDimZ = 1;
  config.attrs = &attribute;
  config.numAttrs = 1;
  check("launch", cuLaunchKernelEx(&config, function, arguments, nullptr));
  check("sync", cuCtxSynchronize());
  std::vector<unsigned char> host(bytes);
  check("read", cuMemcpyDtoH(host.data(), output, bytes));
  std::ofstream(argv[5], std::ios::binary).write(
      reinterpret_cast<const char *>(host.data()), host.size());
  check("read_work", cuMemcpyDtoH(host.data(), work, bytes));
  std::ofstream(argv[6], std::ios::binary).write(
      reinterpret_cast<const char *>(host.data()), host.size());
  check("read_aux", cuMemcpyDtoH(host.data(), auxiliary, bytes));
  std::ofstream(argv[7], std::ios::binary).write(
      reinterpret_cast<const char *>(host.data()), host.size());
  return 0;
}
