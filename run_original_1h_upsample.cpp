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

static std::vector<unsigned char> read(const char *path, size_t size) {
  std::vector<unsigned char> data(size);
  std::ifstream(path, std::ios::binary).read(
      reinterpret_cast<char *>(data.data()), data.size());
  return data;
}

int main(int argc, char **argv) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: %s input skip weights output\n", argv[0]);
    return 2;
  }
  constexpr size_t arenaBytes = 4 * 1024 * 1024;
  constexpr size_t skipBytes = 8 * 1024 * 1024;
  constexpr size_t weightBytes = 22784;
  constexpr int width = 256, height = 144;
  auto input = read(argv[1], arenaBytes);
  auto skip = read(argv[2], skipBytes);
  auto weights = read(argv[3], weightBytes);
  check("init", cuInit(0));
  CUdevice device;
  check("device", cuDeviceGet(&device, 0));
  CUcontext context;
  check("context", cuDevicePrimaryCtxRetain(&context, device));
  check("set", cuCtxSetCurrent(context));
  CUmodule module;
  check("module",
        cuModuleLoad(&module, "/tmp/dlssnr-cubins/dlssnr-00.cubin"));
  CUfunction function;
  check("function", cuModuleGetFunction(
                        &function, module,
                        "cc_tinlayout_fused_swin_1h_32_1_upsample_fp8"));
  CUdeviceptr inputDevice, skipDevice, outputDevice, weightDevice;
  check("input alloc", cuMemAlloc(&inputDevice, input.size()));
  check("skip alloc", cuMemAlloc(&skipDevice, skip.size()));
  check("output alloc", cuMemAlloc(&outputDevice, arenaBytes));
  check("weight alloc", cuMemAlloc(&weightDevice, weights.size()));
  check("input copy", cuMemcpyHtoD(inputDevice, input.data(), input.size()));
  check("skip copy", cuMemcpyHtoD(skipDevice, skip.data(), skip.size()));
  check("weight copy",
        cuMemcpyHtoD(weightDevice, weights.data(), weights.size()));
  check("output clear", cuMemsetD8(outputDevice, 0, arenaBytes));
  alignas(8) unsigned char params[0x60]{};
  std::memcpy(params + 0x00, &inputDevice, 8);
  std::memcpy(params + 0x08, &outputDevice, 8);
  std::memcpy(params + 0x10, &weightDevice, 8);
  std::memcpy(params + 0x18, &height, 4);
  std::memcpy(params + 0x1c, &width, 4);
  std::memcpy(params + 0x40, &inputDevice, 8);
  const int halfHeight = 72, halfWidth = 128;
  std::memcpy(params + 0x48, &halfHeight, 4);
  std::memcpy(params + 0x4c, &halfWidth, 4);
  std::memcpy(params + 0x50, &skipDevice, 8);
  std::memcpy(params + 0x58, &height, 4);
  std::memcpy(params + 0x5c, &width, 4);
  void *arguments[] = {params};
  check("launch", cuLaunchKernel(function, 32, 18, 1, 32, 1, 1, 0,
                                   nullptr, arguments, nullptr));
  check("sync", cuCtxSynchronize());
  std::vector<unsigned char> output(arenaBytes);
  check("download",
        cuMemcpyDtoH(output.data(), outputDevice, output.size()));
  std::ofstream(argv[4], std::ios::binary).write(
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
