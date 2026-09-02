#define wmain nvapi_repack_unused_wmain
#include "d3d12_nvapi_repack_test.cpp"
#undef wmain

struct FusedParams {
  NvU64 main, output, weights, skip;
  NvU64 dimensions;
  NvU64 field;
  NvU64 optional0, optional1, auxiliary, optional3, optional4;
};
static_assert(sizeof(FusedParams) == 0x58);

struct NvapiKernelEx {
  NVDX_ObjectHandle hFunction;
  NVAPI_DIM3 gridDim, blockDim;
  NvU32 dynSharedMemBytes;
  void const *pParams;
  NvU32 paramSize;
  void **kernelParams;
};
static NvAPI_Status NvAPI_D3D12_LaunchCuKernelChainEx(
    ID3D12GraphicsCommandList *commands, const NvapiKernelEx *kernels, NvU32 count) {
  using Function = NvAPI_Status(__cdecl *)(
    ID3D12GraphicsCommandList *, const NvapiKernelEx *, NvU32);
  return reinterpret_cast<Function>(qi()(0x846a9bf0))(commands, kernels, count);
}

struct ResourcePair {
  ID3D12Resource *upload = nullptr;
  ID3D12Resource *device = nullptr;
  UINT64 size = 0;
};

int wmain(int ac, wchar_t **av) {
  if (ac != 9) {
    std::fwprintf(stderr,
      L"usage: %ls cubin arena main skip aux initial-output expected output\n", av[0]);
    return 2;
  }
  auto cubin = rd(av[1]), arena = rd(av[2]), main = rd(av[3]);
  auto skip = rd(av[4]), aux = rd(av[5]), initial = rd(av[6]);
  auto expected = rd(av[7]);
  if (arena.size() != 147719680 || main.size() < (64ull << 20) ||
      skip.size() < (64ull << 20) || aux.size() < (64ull << 20) ||
      initial.size() != (64ull << 20) || expected.size() != initial.size()) return 2;

  IDXGIFactory6 *factory = nullptr;
  hr("factory", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  IDXGIAdapter1 *adapter = nullptr; DXGI_ADAPTER_DESC1 adapter_desc{};
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1 *candidate = nullptr;
    if (factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 desc{}; candidate->GetDesc1(&desc);
    if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        wcsstr(desc.Description, L"NVIDIA")) {
      adapter = candidate; adapter_desc = desc; break;
    }
    candidate->Release();
  }
  if (!adapter) return 1;
  ID3D12Device *device = nullptr;
  hr("device", D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                 IID_PPV_ARGS(&device)));
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  ID3D12CommandQueue *queue = nullptr;
  hr("queue", device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
  ID3D12CommandAllocator *allocator = nullptr;
  hr("allocator", device->CreateCommandAllocator(
    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
  ID3D12GraphicsCommandList *commands = nullptr;
  hr("commands", device->CreateCommandList(
    0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
    IID_PPV_ARGS(&commands)));
  auto upload_heap = hp(D3D12_HEAP_TYPE_UPLOAD);
  auto device_heap = hp(D3D12_HEAP_TYPE_DEFAULT);
  auto readback_heap = hp(D3D12_HEAP_TYPE_READBACK);
  std::vector<ResourcePair> resources;
  auto upload = [&](const std::vector<unsigned char> &data) -> ID3D12Resource * {
    ResourcePair pair{}; pair.size = data.size();
    auto plain = bd(pair.size);
    auto gpu = bd(pair.size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr("upload resource", device->CreateCommittedResource(
      &upload_heap, D3D12_HEAP_FLAG_NONE, &plain,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pair.upload)));
    hr("device resource", device->CreateCommittedResource(
      &device_heap, D3D12_HEAP_FLAG_NONE, &gpu,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pair.device)));
    void *mapped = nullptr; D3D12_RANGE empty{0, 0};
    hr("map", pair.upload->Map(0, &empty, &mapped));
    std::memcpy(mapped, data.data(), data.size()); pair.upload->Unmap(0, nullptr);
    commands->CopyBufferRegion(pair.device, 0, pair.upload, 0, pair.size);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pair.device;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commands->ResourceBarrier(1, &barrier);
    resources.push_back(pair);
    return pair.device;
  };
  ID3D12Resource *arena_resource = upload(arena);
  ID3D12Resource *main_resource = upload(main);
  ID3D12Resource *skip_resource = upload(skip);
  ID3D12Resource *aux_resource = upload(aux);
  ID3D12Resource *output_resource = upload(initial);

  nv("NvAPI_Initialize", NvAPI_Initialize());
  NVDX_ObjectHandle module = nullptr, function = nullptr;
  nv("CreateCuModule", NvAPI_D3D12_CreateCuModule(
    device, cubin.data(), static_cast<NvU32>(cubin.size()), &module));
  nv("CreateCuFunction", NvAPI_D3D12_CreateCuFunction(
    device, module,
    "cc_tinlayout_fused_swin_8h_256_8_upsample_tilesync_fp8", &function));
  constexpr UINT64 view_offset = 0x2800;
  FusedParams params{};
  params.main = main_resource->GetGPUVirtualAddress() + view_offset;
  params.output = output_resource->GetGPUVirtualAddress() + view_offset;
  params.weights = arena_resource->GetGPUVirtualAddress() + 140034048;
  params.skip = skip_resource->GetGPUVirtualAddress() + view_offset;
  params.dimensions = 136ull | (240ull << 32);
  params.auxiliary = aux_resource->GetGPUVirtualAddress();
  NvapiKernelEx launch{};
  launch.hFunction = function;
  launch.gridDim = {30, 17, 1};
  launch.blockDim = {32, 8, 1};
  launch.pParams = &params;
  launch.paramSize = sizeof(params);
  nv("LaunchCuKernelChainEx", NvAPI_D3D12_LaunchCuKernelChainEx(commands, &launch, 1));

  D3D12_RESOURCE_BARRIER output_barrier{};
  output_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  output_barrier.Transition.pResource = output_resource;
  output_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  output_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  output_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  commands->ResourceBarrier(1, &output_barrier);
  auto readback_desc = bd(initial.size());
  ID3D12Resource *readback = nullptr;
  hr("readback", device->CreateCommittedResource(
    &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
  commands->CopyBufferRegion(readback, 0, output_resource, 0, initial.size());
  hr("close", commands->Close());
  ID3D12CommandList *lists[] = {commands}; queue->ExecuteCommandLists(1, lists);
  ID3D12Fence *fence = nullptr;
  hr("fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  hr("signal", queue->Signal(fence, 1));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  hr("event", fence->SetEventOnCompletion(1, event));
  WaitForSingleObject(event, INFINITE); CloseHandle(event);
  void *mapped = nullptr; D3D12_RANGE all{0, initial.size()};
  hr("readback map", readback->Map(0, &all, &mapped));
  const auto *got = static_cast<const uint8_t *>(mapped);
  size_t equal = 0, written_equal = 0, written = 136ull * 240 * 256;
  for (size_t index = 0; index < expected.size(); ++index) equal += got[index] == expected[index];
  for (size_t index = view_offset; index < view_offset + written; ++index)
    written_equal += got[index] == expected[index];
  std::ofstream(av[8], std::ios::binary).write(
    reinterpret_cast<const char *>(got), expected.size());
  readback->Unmap(0, nullptr);
  std::wprintf(L"adapter=%ls full_equal=%zu/%zu written_equal=%zu/%zu\n",
    adapter_desc.Description, equal, expected.size(), written_equal, written);
  return written_equal == written ? 0 : 3;
}
