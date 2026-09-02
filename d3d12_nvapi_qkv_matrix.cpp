#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using NvU32 = uint32_t;
using NVDX_ObjectHandle = void *;
struct DIM3 { NvU32 x, y, z; };
struct Kernel {
  NVDX_ObjectHandle function;
  DIM3 grid, block;
  NvU32 shared;
  const void *params;
  NvU32 paramSize;
};
using Query = void *(__cdecl *)(unsigned);
static Query query() {
  static Query value = reinterpret_cast<Query>(GetProcAddress(
      LoadLibraryW(L"nvapi64.dll"), "nvapi_QueryInterface"));
  return value;
}
static int nv_init() {
  using F = int(__cdecl *)();
  return reinterpret_cast<F>(query()(0x0150E828))();
}
static int create_module(ID3D12Device *device, const void *data, NvU32 size,
                         void **module) {
  using F = int(__cdecl *)(ID3D12Device *, const void *, NvU32, void **);
  return reinterpret_cast<F>(query()(0xad1a677d))(device, data, size, module);
}
static int create_function(ID3D12Device *device, void *module,
                           const char *name, void **function) {
  using F = int(__cdecl *)(ID3D12Device *, void *, const char *, void **);
  return reinterpret_cast<F>(query()(0xe2436e22))(
      device, module, name, function);
}
static int launch_chain(ID3D12GraphicsCommandList *commands,
                        const Kernel *kernels, NvU32 count) {
  struct P {
    NVDX_ObjectHandle function;
    DIM3 grid, block;
    NvU32 shared;
    const void *params;
    NvU32 paramSize;
    void **kernelParams;
  };
  std::vector<P> values;
  for (NvU32 i = 0; i < count; ++i)
    values.push_back({kernels[i].function, kernels[i].grid, kernels[i].block,
                      kernels[i].shared, kernels[i].params,
                      kernels[i].paramSize, nullptr});
  using F = int(__cdecl *)(ID3D12GraphicsCommandList *, const P *, NvU32);
  return reinterpret_cast<F>(query()(0x846a9bf0))(
      commands, values.data(), count);
}
static void hr(const char *name, HRESULT result) {
  if (FAILED(result)) {
    std::fprintf(stderr, "%s=%08lx\n", name, result);
    ExitProcess(1);
  }
}
static void nv(const char *name, int result) {
  if (result) {
    std::fprintf(stderr, "%s=%d\n", name, result);
    ExitProcess(1);
  }
}
static std::vector<uint8_t> read_file(const wchar_t *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) ExitProcess(2);
  size_t size = static_cast<size_t>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> value(size);
  file.read(reinterpret_cast<char *>(value.data()), size);
  return value;
}
static D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES value{};
  value.Type = type;
  value.CreationNodeMask = value.VisibleNodeMask = 1;
  return value;
}
static D3D12_RESOURCE_DESC buffer(UINT64 size,
                                  D3D12_RESOURCE_FLAGS flags =
                                      D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC value{};
  value.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  value.Width = size;
  value.Height = 1;
  value.DepthOrArraySize = 1;
  value.MipLevels = 1;
  value.SampleDesc.Count = 1;
  value.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  value.Flags = flags;
  return value;
}
static void put64(std::array<uint8_t, 0x50> &blob, size_t offset,
                  uint64_t value) {
  std::memcpy(blob.data() + offset, &value, 8);
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 11) {
    std::fwprintf(stderr, L"usage: %ls cubin arena main work aux input-offs "
                           L"q-offs k-offs v-offs output\n", argv[0]);
    return 2;
  }
  constexpr UINT64 A = 2 * 1024 * 1024, WA = 147719680;
  auto cubin = read_file(argv[1]), weights = read_file(argv[2]),
       mainState = read_file(argv[3]), workState = read_file(argv[4]),
       auxState = read_file(argv[5]), inputOffsetBytes = read_file(argv[6]),
       qOffsetBytes = read_file(argv[7]), kOffsetBytes = read_file(argv[8]),
       vOffsetBytes = read_file(argv[9]);
  if (weights.size() != WA || mainState.size() != A || workState.size() != A ||
      auxState.size() != A || inputOffsetBytes.size() != 4096 ||
      qOffsetBytes.size() != 4096 || kOffsetBytes.size() != 4096 ||
      vOffsetBytes.size() != 4096) return 2;
  const uint32_t *inputOffsets =
      reinterpret_cast<const uint32_t *>(inputOffsetBytes.data());
  const uint32_t *outputOffsets[3] = {
      reinterpret_cast<const uint32_t *>(qOffsetBytes.data()),
      reinterpret_cast<const uint32_t *>(kOffsetBytes.data()),
      reinterpret_cast<const uint32_t *>(vOffsetBytes.data())};

  IDXGIFactory6 *factory = nullptr;
  hr("factory", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  IDXGIAdapter1 *adapter = nullptr;
  DXGI_ADAPTER_DESC1 adapterDesc{};
  for (UINT i = 0;; ++i) {
    IDXGIAdapter1 *candidate = nullptr;
    if (factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
    candidate->GetDesc1(&adapterDesc);
    if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        wcsstr(adapterDesc.Description, L"NVIDIA")) {
      adapter = candidate;
      break;
    }
    candidate->Release();
  }
  if (!adapter) return 1;
  ID3D12Device *device = nullptr;
  hr("device", D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                  IID_PPV_ARGS(&device)));
  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  ID3D12CommandQueue *queue = nullptr;
  hr("queue", device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
  ID3D12Fence *fence = nullptr;
  hr("fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&fence)));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  UINT64 fenceValue = 0;

  auto uploadHeap = heap(D3D12_HEAP_TYPE_UPLOAD);
  auto defaultHeap = heap(D3D12_HEAP_TYPE_DEFAULT);
  auto readbackHeap = heap(D3D12_HEAP_TYPE_READBACK);
  auto make = [&](UINT64 size, D3D12_HEAP_PROPERTIES *properties,
                  D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    ID3D12Resource *resource = nullptr;
    auto desc = buffer(size, flags);
    hr("resource", device->CreateCommittedResource(
                       properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                       IID_PPV_ARGS(&resource)));
    return resource;
  };
  ID3D12Resource *uploadWeights = make(
      WA, &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *uploadMain = make(
      A, &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *uploadWork = make(
      A, &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *uploadAux = make(
      A, &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *zero = make(A, &uploadHeap,
                              D3D12_RESOURCE_STATE_GENERIC_READ,
                              D3D12_RESOURCE_FLAG_NONE);
  void *mapped = nullptr;
  D3D12_RANGE empty{0, 0};
  uploadWeights->Map(0, &empty, &mapped);
  std::memcpy(mapped, weights.data(), WA);
  uploadWeights->Unmap(0, nullptr);
  uploadMain->Map(0, &empty, &mapped);
  std::memset(mapped, 0, A);
  auto *mappedMain = reinterpret_cast<uint8_t *>(mapped);
  uploadWork->Map(0, &empty, &mapped);
  std::memcpy(mapped, workState.data(), A);
  uploadWork->Unmap(0, nullptr);
  uploadAux->Map(0, &empty, &mapped);
  std::memcpy(mapped, auxState.data(), A);
  uploadAux->Unmap(0, nullptr);
  zero->Map(0, &empty, &mapped);
  std::memset(mapped, 0, A);
  zero->Unmap(0, nullptr);

  ID3D12Resource *weightResource = make(
      WA, &defaultHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *mainResource = make(
      A, &defaultHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *workResource = make(
      A, &defaultHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *auxResource = make(
      A, &defaultHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *outputs[3];
  for (auto &output : outputs)
    output = make(A, &defaultHeap, D3D12_RESOURCE_STATE_COPY_DEST,
                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *readback = make(
      3 * A, &readbackHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_NONE);

  auto submit = [&](ID3D12GraphicsCommandList *commands) {
    hr("close", commands->Close());
    ID3D12CommandList *lists[] = {commands};
    queue->ExecuteCommandLists(1, lists);
    hr("signal", queue->Signal(fence, ++fenceValue));
    hr("fence_event", fence->SetEventOnCompletion(fenceValue, event));
    WaitForSingleObject(event, INFINITE);
    HRESULT removed = device->GetDeviceRemovedReason();
    if (FAILED(removed)) {
      std::fprintf(stderr, "device_removed=%08lx\n", removed);
      ExitProcess(1);
    }
  };
  ID3D12CommandAllocator *allocator = nullptr;
  ID3D12GraphicsCommandList *commands = nullptr;
  hr("allocator", device->CreateCommandAllocator(
                      D3D12_COMMAND_LIST_TYPE_DIRECT,
                      IID_PPV_ARGS(&allocator)));
  hr("commands", device->CreateCommandList(
                     0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                     IID_PPV_ARGS(&commands)));
  commands->CopyBufferRegion(weightResource, 0, uploadWeights, 0, WA);
  D3D12_RESOURCE_BARRIER weightBarrier{};
  weightBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  weightBarrier.Transition.pResource = weightResource;
  weightBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  weightBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  weightBarrier.Transition.StateAfter =
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  commands->ResourceBarrier(1, &weightBarrier);
  submit(commands);
  commands->Release();
  allocator->Release();

  nv("nv_init", nv_init());
  void *module = nullptr, *standard = nullptr, *chained = nullptr;
  nv("module", create_module(device, cubin.data(),
                              static_cast<NvU32>(cubin.size()), &module));
  nv("standard", create_function(device, module, "cc_vit_1d_qkv_fp8",
                                   &standard));
  nv("chained", create_function(device, module,
                                  "cc_vit_1d_qkv_chained_fp8", &chained));
  std::array<uint8_t, 0x50> params{};
  put64(params, 0, mainResource->GetGPUVirtualAddress());
  put64(params, 8, outputs[0]->GetGPUVirtualAddress());
  put64(params, 16, outputs[1]->GetGPUVirtualAddress());
  put64(params, 24, outputs[2]->GetGPUVirtualAddress());
  put64(params, 32, weightResource->GetGPUVirtualAddress() + 0x1df6c00);
  put64(params, 40, auxResource->GetGPUVirtualAddress() + 0x1200);
  put64(params, 48, workResource->GetGPUVirtualAddress());
  put64(params, 56, auxResource->GetGPUVirtualAddress() + 0xe00);
  put64(params, 64, auxResource->GetGPUVirtualAddress() + 0x1800);
  put64(params, 72, 8ull | (8ull << 32));
  Kernel kernels[2] = {
      {standard, {16, 1, 2}, {32, 4, 1}, 0, params.data(), 0x50},
      {chained, {16, 1, 2}, {32, 4, 1}, 0, params.data(), 0x50}};
  std::ofstream destination(argv[10], std::ios::binary);
  std::vector<uint8_t> hostReadback(3 * A);
  bool first = true;
  for (unsigned basis = 0; basis < 1024; ++basis) {
    if (basis) mappedMain[inputOffsets[basis - 1]] = 0;
    mappedMain[inputOffsets[basis]] = 0x38;
    hr("allocator", device->CreateCommandAllocator(
                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(&allocator)));
    hr("commands", device->CreateCommandList(
                       0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                       IID_PPV_ARGS(&commands)));
    std::vector<D3D12_RESOURCE_BARRIER> toCopy;
    auto transition = [&](ID3D12Resource *resource,
                          D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after) {
      D3D12_RESOURCE_BARRIER value{};
      value.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      value.Transition.pResource = resource;
      value.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      value.Transition.StateBefore = before;
      value.Transition.StateAfter = after;
      toCopy.push_back(value);
    };
    if (!first) {
      transition(mainResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      transition(workResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      transition(auxResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      for (auto output : outputs)
        transition(output, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
      commands->ResourceBarrier(static_cast<UINT>(toCopy.size()),
                                toCopy.data());
    }
    commands->CopyBufferRegion(mainResource, 0, uploadMain, 0, A);
    commands->CopyBufferRegion(workResource, 0, uploadWork, 0, A);
    commands->CopyBufferRegion(auxResource, 0, uploadAux, 0, A);
    for (auto output : outputs)
      commands->CopyBufferRegion(output, 0, zero, 0, A);
    std::vector<D3D12_RESOURCE_BARRIER> toUav;
    auto uavTransition = [&](ID3D12Resource *resource) {
      D3D12_RESOURCE_BARRIER value{};
      value.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      value.Transition.pResource = resource;
      value.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      value.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      value.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      toUav.push_back(value);
    };
    uavTransition(mainResource);
    uavTransition(workResource);
    uavTransition(auxResource);
    for (auto output : outputs) uavTransition(output);
    commands->ResourceBarrier(static_cast<UINT>(toUav.size()), toUav.data());
    nv("qkv_chain", launch_chain(commands, kernels, 2));
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &uav);
    std::vector<D3D12_RESOURCE_BARRIER> toRead;
    for (auto output : outputs) {
      D3D12_RESOURCE_BARRIER value{};
      value.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      value.Transition.pResource = output;
      value.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      value.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      value.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      toRead.push_back(value);
    }
    commands->ResourceBarrier(static_cast<UINT>(toRead.size()), toRead.data());
    for (int group = 0; group < 3; ++group)
      commands->CopyBufferRegion(readback, group * A, outputs[group], 0, A);
    submit(commands);
    commands->Release();
    allocator->Release();
    D3D12_RANGE range{0, 3 * A};
    readback->Map(0, &range, &mapped);
    std::memcpy(hostReadback.data(), mapped, 3 * A);
    readback->Unmap(0, &empty);
    for (int group = 0; group < 3; ++group)
      for (int output = 0; output < 1024; ++output)
        destination.put(static_cast<char>(
            hostReadback[group * A + outputOffsets[group][output]]));
    first = false;
    if ((basis & 127) == 127)
      std::fprintf(stderr, "basis=%u/1024\n", basis + 1);
  }
  uploadMain->Unmap(0, nullptr);
  std::printf("adapter=%ls basis=1024 output_bytes=%u\n",
              adapterDesc.Description, 1024 * 3 * 1024);
  return 0;
}
