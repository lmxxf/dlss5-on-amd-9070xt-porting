#include <cuda.h>
#include <cstdint>
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
  if (argc != 6 && argc != 7) {
    std::fprintf(stderr,
                 "usage: %s weight-arena weight-offset basis-start "
                 "basis-count output [basis-stride]\n",
                 argv[0]);
    return 2;
  }
  constexpr size_t arenaBytes = 2 * 1024 * 1024;
  constexpr size_t weightBytes = 147719680;
  // The 64 logical 4096-byte token outputs occupy a sparse 2 MiB physical
  // view; do not truncate this to the 256 KiB logical payload.
  constexpr size_t activeOutputBytes = arenaBytes;
  constexpr unsigned char one = 0x38;
  const uint64_t weightOffset = std::strtoull(argv[2], nullptr, 0);
  const unsigned basisStart = std::strtoul(argv[3], nullptr, 0);
  const unsigned basisCount = std::strtoul(argv[4], nullptr, 0);
  const unsigned basisStride = argc == 7 ? std::strtoul(argv[6], nullptr, 0) : 1;
  if (!basisCount || !basisStride ||
      basisStart + (basisCount - 1) * basisStride >= 64 * 1024)
    return 2;
  auto weights = read_file(argv[1], weightBytes);

  check("init", cuInit(0));
  CUdevice device;
  check("device", cuDeviceGet(&device, 0));
  CUcontext context;
  check("context", cuDevicePrimaryCtxRetain(&context, device));
  check("set", cuCtxSetCurrent(context));
  CUmodule module;
  check("module",
        cuModuleLoad(&module, "/tmp/dlssnr-cubins/dlssnr-05.cubin"));
  CUfunction expand;
  check("function", cuModuleGetFunction(&expand, module,
                                         "cc_vit_1d_ffn_expand_fp8"));
  CUdeviceptr input, output, auxiliary, deviceWeights;
  for (CUdeviceptr *pointer : {&input, &output, &auxiliary}) {
    check("alloc", cuMemAlloc(pointer, arenaBytes));
    check("zero", cuMemsetD8(*pointer, 0, arenaBytes));
  }
  check("weight_alloc", cuMemAlloc(&deviceWeights, weightBytes));
  check("weight_upload",
        cuMemcpyHtoD(deviceWeights, weights.data(), weightBytes));
  uint64_t tokens = 64;
  unsigned char params[0x48]{};
  std::memcpy(params + 0, &input, 8);
  std::memcpy(params + 16, &output, 8);
  CUdeviceptr blockWeights = deviceWeights + weightOffset;
  std::memcpy(params + 24, &blockWeights, 8);
  std::memcpy(params + 56, &auxiliary, 8);
  std::memcpy(params + 64, &tokens, 8);
  void *arguments[] = {params};
  std::vector<unsigned char> host(activeOutputBytes);
  std::ofstream destination(argv[5], std::ios::binary);
  for (unsigned i = 0; i < basisCount; ++i) {
    const unsigned basis = basisStart + i * basisStride;
    check("input_clear", cuMemsetD8(input, 0, arenaBytes));
    check("output_clear", cuMemsetD8(output, 0, arenaBytes));
    check("aux_clear", cuMemsetD8(auxiliary, 0, arenaBytes));
    check("basis", cuMemsetD8(input + basis, one, 1));
    check("launch", cuLaunchKernel(expand, 32, 1, 1, 32, 4, 1, 0,
                                    nullptr, arguments, nullptr));
    check("sync", cuCtxSynchronize());
    check("read", cuMemcpyDtoH(host.data(), output, host.size()));
    destination.write(reinterpret_cast<const char *>(host.data()), host.size());
  }
  std::printf("basis_start=%u basis_count=%u basis_stride=%u "
              "bytes_per_basis=%zu\n",
              basisStart, basisCount, basisStride, activeOutputBytes);
  return 0;
}
