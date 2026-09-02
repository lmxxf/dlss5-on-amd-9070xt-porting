#include <cuda.h>
#include <array>
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
  if (argc != 11) {
    std::fprintf(stderr, "usage: %s arena weight-offset work aux "
                         "input-offsets.i32 matrix.fp8 q-offsets.i32 "
                         "k-offsets.i32 v-offsets.i32 metadata.txt\n", argv[0]);
    return 2;
  }
  constexpr size_t arenaBytes = 2 * 1024 * 1024;
  constexpr size_t weightBytes = 147719680;
  constexpr unsigned char one = 0x38;
  auto weights = read_file(argv[1], weightBytes);
  auto hostWork = read_file(argv[3], arenaBytes);
  auto hostAux = read_file(argv[4], arenaBytes);
  auto inputOffsetBytes = read_file(argv[5], 1024 * 4);
  const auto *inputOffsets =
      reinterpret_cast<const uint32_t *>(inputOffsetBytes.data());

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
                                         "cc_vit_1d_qkv_fp8"));
  CUdeviceptr input, output[3], work, auxiliary, deviceWeights;
  for (CUdeviceptr *pointer :
       {&input, &output[0], &output[1], &output[2], &work, &auxiliary}) {
    check("alloc", cuMemAlloc(pointer, arenaBytes));
    check("zero", cuMemsetD8(*pointer, 0, arenaBytes));
  }
  check("weight_alloc", cuMemAlloc(&deviceWeights, weightBytes));
  check("weight_upload",
        cuMemcpyHtoD(deviceWeights, weights.data(), weightBytes));
  check("work_upload", cuMemcpyHtoD(work, hostWork.data(), arenaBytes));
  check("aux_upload", cuMemcpyHtoD(auxiliary, hostAux.data(), arenaBytes));
  CUdeviceptr a2 = auxiliary + 0xe00, a3 = auxiliary + 0x1200,
              a4 = auxiliary + 0x1800,
              blockWeights =
                  deviceWeights + std::strtoull(argv[2], nullptr, 0);
  uint64_t dimensions = 8ull | (8ull << 32);
  unsigned char params[0x50]{};
  std::memcpy(params + 0, &input, 8);
  std::memcpy(params + 8, &output[0], 8);
  std::memcpy(params + 16, &output[1], 8);
  std::memcpy(params + 24, &output[2], 8);
  std::memcpy(params + 32, &blockWeights, 8);
  std::memcpy(params + 40, &a3, 8);
  std::memcpy(params + 48, &work, 8);
  std::memcpy(params + 56, &a2, 8);
  std::memcpy(params + 64, &a4, 8);
  std::memcpy(params + 72, &dimensions, 8);
  void *arguments[] = {params};
  CUlaunchAttribute attribute{};
  attribute.id = CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;
  attribute.value.clusterDim = {1, 1, 2};
  CUlaunchConfig config{};
  config.gridDimX = 16;
  config.gridDimY = 1;
  config.gridDimZ = 2;
  config.blockDimX = 32;
  config.blockDimY = 4;
  config.blockDimZ = 1;
  config.attrs = &attribute;
  config.numAttrs = 1;

  std::array<std::vector<unsigned char>, 3> hostOutputs = {
      std::vector<unsigned char>(arenaBytes),
      std::vector<unsigned char>(arenaBytes),
      std::vector<unsigned char>(arenaBytes)};
  auto launch = [&](unsigned channel) {
    check("input_clear", cuMemsetD8(input, 0, arenaBytes));
    for (auto pointer : output)
      check("output_clear", cuMemsetD8(pointer, 0, arenaBytes));
    check("basis", cuMemsetD8(input + inputOffsets[channel], one, 1));
    check("launch", cuLaunchKernelEx(&config, function, arguments, nullptr));
    check("sync", cuCtxSynchronize());
    for (int group = 0; group < 3; ++group)
      check("read", cuMemcpyDtoH(hostOutputs[group].data(), output[group],
                                  arenaBytes));
  };

  std::array<std::vector<unsigned char>, 3> support = {
      std::vector<unsigned char>(arenaBytes),
      std::vector<unsigned char>(arenaBytes),
      std::vector<unsigned char>(arenaBytes)};
  for (unsigned channel = 0; channel < 16; ++channel) {
    launch(channel);
    for (int group = 0; group < 3; ++group)
      for (size_t i = 0; i < arenaBytes; ++i)
        support[group][i] |= hostOutputs[group][i] != 0;
  }
  std::array<std::vector<uint32_t>, 3> outputOffsets;
  for (int group = 0; group < 3; ++group) {
    for (uint32_t i = 0; i < arenaBytes; ++i)
      if (support[group][i])
        outputOffsets[group].push_back(i);
    if (outputOffsets[group].size() != 1024) {
      std::fprintf(stderr, "group%d support=%zu expected=1024\n", group,
                   outputOffsets[group].size());
      return 3;
    }
  }

  std::ofstream matrix(argv[6], std::ios::binary);
  for (unsigned channel = 0; channel < 1024; ++channel) {
    launch(channel);
    for (int group = 0; group < 3; ++group)
      for (uint32_t offset : outputOffsets[group])
        matrix.put(static_cast<char>(hostOutputs[group][offset]));
  }
  for (int group = 0; group < 3; ++group)
    std::ofstream(argv[7 + group], std::ios::binary).write(
        reinterpret_cast<const char *>(outputOffsets[group].data()),
        outputOffsets[group].size() * sizeof(uint32_t));
  std::ofstream metadata(argv[10]);
  metadata << "inputs=1024 groups=3 outputs_per_group=1024 basis=E4M3_1.0\n";
  std::printf("inputs=1024 groups=3 matrix_bytes=%u\n", 1024 * 3 * 1024);
  return 0;
}
