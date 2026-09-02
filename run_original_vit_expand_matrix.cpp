#include <cuda.h>
#include <algorithm>
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

// Basis scans show that the 32 KiB main view interleaves 32 tokens and 1024
// channels with these bit assignments.
static uint32_t physical_input(unsigned token, unsigned channel) {
  constexpr unsigned channelBits[] = {0, 1, 3, 4, 5, 9, 10, 11, 12, 13};
  uint32_t result = ((token & 1) << 2) | (((token >> 1) & 7) << 6) |
                    (((token >> 4) & 1) << 14);
  for (unsigned bit = 0; bit < 10; ++bit)
    result |= ((channel >> bit) & 1) << channelBits[bit];
  return result;
}

int main(int argc, char **argv) {
  if (argc != 7 && argc != 8) {
    std::fprintf(stderr,
                 "usage: %s weight-arena weight-offset token matrix.fp8 "
                 "input-offsets.i32 output-offsets.i32 [basis-e4m3-byte]\n",
                 argv[0]);
    return 2;
  }
  constexpr size_t arenaBytes = 2 * 1024 * 1024;
  constexpr size_t weightBytes = 147719680;
  constexpr unsigned char one = 0x38;
  const uint64_t weightOffset = std::strtoull(argv[2], nullptr, 0);
  const unsigned token = std::strtoul(argv[3], nullptr, 0);
  const bool hadamard = argc == 8 && std::strcmp(argv[7], "hadamard") == 0;
  const unsigned basisValue =
      hadamard ? 0x10
               : (argc == 8 ? std::strtoul(argv[7], nullptr, 0) : one);
  if (token >= 32 || basisValue > 255)
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
  std::vector<unsigned char> host(arenaBytes);
  std::vector<unsigned char> hostInput(arenaBytes);
  auto launch = [&](unsigned channel) {
    check("input_clear", cuMemsetD8(input, 0, arenaBytes));
    check("output_clear", cuMemsetD8(output, 0, arenaBytes));
    check("aux_clear", cuMemsetD8(auxiliary, 0, arenaBytes));
    check("basis", cuMemsetD8(input + physical_input(token, channel),
                               basisValue, 1));
    check("launch", cuLaunchKernel(expand, 32, 1, 1, 32, 4, 1, 0,
                                    nullptr, arguments, nullptr));
    check("sync", cuCtxSynchronize());
    check("read", cuMemcpyDtoH(host.data(), output, host.size()));
  };

  // Sixteen random-looking columns are enough to cover every physical output
  // slot for the dense 4096-wide matrix.  Validate the expected cardinality
  // instead of silently accepting a partial support set.
  std::vector<unsigned char> support(arenaBytes, 0);
  for (unsigned channel = 0; channel < 16; ++channel) {
    launch(channel);
    for (size_t i = 0; i < arenaBytes; ++i)
      support[i] |= host[i] != 0;
  }
  std::vector<uint32_t> outputOffsets;
  for (uint32_t i = 0; i < arenaBytes; ++i)
    if (support[i])
      outputOffsets.push_back(i);
  if (outputOffsets.size() != 4096) {
    std::fprintf(stderr, "output support=%zu expected=4096\n",
                 outputOffsets.size());
    return 3;
  }

  std::vector<uint32_t> inputOffsets(1024);
  std::ofstream matrix(argv[4], std::ios::binary);
  for (unsigned channel = 0; channel < 1024; ++channel)
    inputOffsets[channel] = physical_input(token, channel);
  for (unsigned probe = 0; probe < 1024; ++probe) {
    if (hadamard) {
      std::fill(hostInput.begin(), hostInput.end(), 0);
      for (unsigned channel = 0; channel < 1024; ++channel) {
        const bool negative = __builtin_parity(probe & channel);
        hostInput[inputOffsets[channel]] =
            static_cast<unsigned char>(basisValue | (negative ? 0x80 : 0));
      }
      check("input_upload", cuMemcpyHtoD(input, hostInput.data(), arenaBytes));
      check("output_clear", cuMemsetD8(output, 0, arenaBytes));
      check("aux_clear", cuMemsetD8(auxiliary, 0, arenaBytes));
      check("launch", cuLaunchKernel(expand, 32, 1, 1, 32, 4, 1, 0,
                                      nullptr, arguments, nullptr));
      check("sync", cuCtxSynchronize());
      check("read", cuMemcpyDtoH(host.data(), output, host.size()));
    } else {
      launch(probe);
    }
    for (uint32_t offset : outputOffsets)
      matrix.put(static_cast<char>(host[offset]));
  }
  std::ofstream(argv[5], std::ios::binary).write(
      reinterpret_cast<const char *>(inputOffsets.data()),
      inputOffsets.size() * sizeof(uint32_t));
  std::ofstream(argv[6], std::ios::binary).write(
      reinterpret_cast<const char *>(outputOffsets.data()),
      outputOffsets.size() * sizeof(uint32_t));
  std::printf("token=%u mode=%s basis_e4m3=0x%02x inputs=%zu outputs=%zu "
              "matrix_bytes=%zu\n", token,
              hadamard ? "hadamard" : "basis", basisValue,
              inputOffsets.size(), outputOffsets.size(),
              inputOffsets.size() * outputOffsets.size());
  return 0;
}
