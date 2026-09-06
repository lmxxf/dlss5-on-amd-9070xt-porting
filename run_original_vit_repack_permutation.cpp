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

int main(int argc, char **argv) {
  if (argc != 3 && argc != 5) {
    std::fprintf(stderr, "usage: %s output-to-input.i32 metadata.json [width height]\n",
                 argv[0]);
    return 2;
  }
  constexpr size_t arenaBytes = 4 * 1024 * 1024;
  const int width = argc == 5 ? std::atoi(argv[3]) : 8;
  const int height = argc == 5 ? std::atoi(argv[4]) : 8;
  if(width<=0||height<=0||width%4||height%4||size_t(width)*size_t(height)>arenaBytes/1024)return 2;
  const size_t outputBytes = static_cast<size_t>(width) * height * 1024;
  constexpr unsigned addressBits = 22;
  constexpr unsigned char one = 0x38; // E4M3 1.0

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
                        &repack, module, "cc_vit_1d_repack_2d_to_1d_fp8"));

  CUdeviceptr source, destination;
  check("source", cuMemAlloc(&source, arenaBytes));
  check("destination", cuMemAlloc(&destination, arenaBytes));
  std::vector<unsigned char> input(arenaBytes), output(arenaBytes);
  std::vector<uint32_t> outputToInput(outputBytes, 0);
  std::vector<unsigned char> active(outputBytes, 0);
  unsigned char params[0x18]{};
  std::memcpy(params + 0, &source, 8);
  std::memcpy(params + 8, &destination, 8);
  std::memcpy(params + 16, &width, 4);
  std::memcpy(params + 20, &height, 4);
  void *arguments[] = {params};

  auto launch = [&]() {
    check("upload", cuMemcpyHtoD(source, input.data(), arenaBytes));
    check("clear", cuMemsetD8(destination, 0, arenaBytes));
    check("launch", cuLaunchKernel(repack, width * height + 16, 1, 1, 32, 4, 1, 0,
                                    nullptr, arguments, nullptr));
    check("sync", cuCtxSynchronize());
    check("download", cuMemcpyDtoH(output.data(), destination, arenaBytes));
  };

  std::memset(input.data(), one, input.size());
  launch();
  size_t activeCount = 0, last = 0;
  for (size_t i = 0; i < outputBytes; ++i) {
    active[i] = output[i] == one;
    if (active[i]) {
      ++activeCount;
      last = i;
    }
  }
  if (activeCount != outputBytes) {
    std::fprintf(stderr, "baseline active=%zu expected=%zu last=%zu\n",
                 activeCount, outputBytes, last);
    return 3;
  }

  for (unsigned bit = 0; bit < addressBits; ++bit) {
    for (size_t i = 0; i < arenaBytes; ++i)
      input[i] = ((i >> bit) & 1) ? one : 0;
    launch();
    for (size_t i = 0; i < outputBytes; ++i) {
      if (output[i] == one)
        outputToInput[i] |= 1u << bit;
      else if (output[i] != 0) {
        std::fprintf(stderr, "non-binary output bit=%u offset=%zu value=%u\n",
                     bit, i, output[i]);
        return 4;
      }
    }
  }

  std::vector<unsigned char> seen(arenaBytes, 0);
  for (uint32_t sourceOffset : outputToInput) {
    if (sourceOffset >= arenaBytes || seen[sourceOffset]) {
      std::fprintf(stderr, "mapping is not injective: source=%u\n",
                   sourceOffset);
      return 5;
    }
    seen[sourceOffset] = 1;
  }
  for(uint32_t seed : {1709u,1721u}) {
    uint32_t state=seed;
    for(auto& value:input){state^=state<<13;state^=state>>17;state^=state<<5;value=static_cast<unsigned char>((state%126+1)|((state>>31)<<7));}
    launch();
    for(size_t i=0;i<outputBytes;i++)if(output[i]!=input[outputToInput[i]]){
      std::fprintf(stderr,"held-out repack mismatch seed=%u offset=%zu\n",seed,i);return 6;
    }
  }
  std::ofstream(argv[1], std::ios::binary).write(
      reinterpret_cast<const char *>(outputToInput.data()),
      outputToInput.size() * sizeof(uint32_t));
  std::ofstream metadata(argv[2]);
  metadata << "{\n"
           << "  \"semantics\": \"1d physical byte offset -> 2d physical byte offset\",\n"
           << "  \"shape\": [" << (width * height) << ", 1024],\n"
           << "  \"entries\": " << outputToInput.size() << ",\n"
           << "  \"source_arena_bytes\": " << arenaBytes << ",\n"
           << "  \"launches\": " << (addressBits + 3) << ",\n"
           << "  \"held_out_random_inputs\": 2,\n"
           << "  \"bijective_over_selected_offsets\": true\n"
           << "}\n";
  std::printf("entries=%zu unique_sources=%zu source_min=%u source_max=%u\n",
              outputToInput.size(), outputToInput.size(),
              *std::min_element(outputToInput.begin(), outputToInput.end()),
              *std::max_element(outputToInput.begin(), outputToInput.end()));
  return 0;
}
