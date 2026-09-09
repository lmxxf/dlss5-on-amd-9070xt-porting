#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static void check(const char *name, HRESULT result) {
  if (FAILED(result)) {
    std::fprintf(stderr, "%s=0x%08lx\n", name, result);
    ExitProcess(1);
  }
}
static std::vector<uint8_t> read_file(const wchar_t *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    ExitProcess(2);
  size_t size = static_cast<size_t>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> result(size);
  file.read(reinterpret_cast<char *>(result.data()), size);
  return result;
}
static float e4m3(uint8_t value) {
  const float sign = value & 0x80 ? -1.0f : 1.0f;
  const unsigned exponent = (value >> 3) & 15, mantissa = value & 7;
  if (!exponent)
    return sign * (mantissa / 8.0f) * std::ldexp(1.0f, -6);
  if (exponent == 15 && mantissa == 7)
    return NAN;
  return sign * (1.0f + mantissa / 8.0f) *
         std::ldexp(1.0f, static_cast<int>(exponent) - 7);
}
static float quantize_e4m3(float value) {
  float best = 0, distance = INFINITY;
  for (unsigned bits = 0; bits < 256; ++bits) {
    float candidate = e4m3(static_cast<uint8_t>(bits));
    if (!std::isfinite(candidate))
      continue;
    float d = std::fabs(candidate - value);
    if (d < distance) {
      distance = d;
      best = candidate;
    }
  }
  return best;
}
static D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES result{};
  result.Type = type;
  result.CreationNodeMask = result.VisibleNodeMask = 1;
  return result;
}
static D3D12_RESOURCE_DESC buffer(UINT64 size,
                                  D3D12_RESOURCE_FLAGS flags =
                                      D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC result{};
  result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  result.Width = size;
  result.Height = 1;
  result.DepthOrArraySize = 1;
  result.MipLevels = 1;
  result.SampleDesc.Count = 1;
  result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  result.Flags = flags;
  return result;
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 6) {
    std::fwprintf(stderr, L"usage: %ls matrix.f16 input.fp8 oracle.fp8 "
                           L"input-offsets.i32 output-offsets.i32\n",
                  argv[0]);
    return 2;
  }
  auto matrix = read_file(argv[1]), physicalInput = read_file(argv[2]),
       physicalOracle = read_file(argv[3]), inputOffsetBytes = read_file(argv[4]),
       outputOffsetBytes = read_file(argv[5]);
  if (matrix.size() != 1024ull * 4096 * 2 ||
      physicalInput.size() != 2 * 1024 * 1024 ||
      physicalOracle.size() != 2 * 1024 * 1024 ||
      inputOffsetBytes.size() != 1024 * 4 ||
      outputOffsetBytes.size() != 4096 * 4)
    return 2;
  const auto *inputOffsets = reinterpret_cast<const uint32_t *>(inputOffsetBytes.data());
  const auto *outputOffsets = reinterpret_cast<const uint32_t *>(outputOffsetBytes.data());
  std::vector<float> input(1024), oracle(4096);
  for (unsigned i = 0; i < 1024; ++i)
    input[i] = e4m3(physicalInput[inputOffsets[i]]);
  for (unsigned i = 0; i < 4096; ++i)
    oracle[i] = e4m3(physicalOracle[outputOffsets[i]]);

  IDXGIFactory6 *factory = nullptr;
  check("factory", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  IDXGIAdapter1 *adapter = nullptr;
  DXGI_ADAPTER_DESC1 adapterDesc{};
  for (UINT i = 0;; ++i) {
    IDXGIAdapter1 *candidate = nullptr;
    if (factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND)
      break;
    candidate->GetDesc1(&adapterDesc);
    if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        wcsstr(adapterDesc.Description, L"AMD")) {
      adapter = candidate;
      break;
    }
    candidate->Release();
  }
  if (!adapter)
    return 1;
  ID3D12Device *device = nullptr;
  check("device", D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                     IID_PPV_ARGS(&device)));
  std::fwprintf(stderr, L"adapter: %ls\n", adapterDesc.Description);

  const char shader[] = R"(
ByteAddressBuffer weights : register(t0);
StructuredBuffer<float> input : register(t1);
RWStructuredBuffer<float> output : register(u0);
float half_weight(uint index) {
  uint pair = weights.Load((index >> 1) * 4);
  return f16tof32((pair >> ((index & 1) * 16)) & 0xffff);
}
float fp8(float x) {
  if (x == 0) return 0;
  float s = x < 0 ? -1 : 1, a = abs(x);
  if (a < .015625) return s * round(a * 512) / 512;
  float e = clamp(floor(log2(a)), -6., 8.);
  float m = round((a / exp2(e) - 1) * 8);
  if (m >= 8) { m = 0; e += 1; }
  return s * min(exp2(e) * (1 + m / 8), 448.);
}
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint o = id.x;
  if (o >= 4096) return;
  float value = 0;
  [loop] for (uint i = 0; i < 1024; ++i)
    value += input[i] * half_weight(i * 4096 + o);
  output[o] = fp8(value);
}
)";
  ID3DBlob *code = nullptr, *error = nullptr;
  HRESULT compiled = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr,
                                nullptr, "main", "cs_5_1",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
  if (error) {
    std::fwrite(error->GetBufferPointer(), 1, error->GetBufferSize(), stderr);
    error->Release();
  }
  check("compile", compiled);
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2};
  D3D12_ROOT_PARAMETER parameter{};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &parameter;
  ID3DBlob *rootBlob = nullptr;
  check("serialize", D3D12SerializeRootSignature(
                         &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob,
                         &error));
  ID3D12RootSignature *root = nullptr;
  check("root", device->CreateRootSignature(
                    0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                    IID_PPV_ARGS(&root)));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
  pipelineDesc.pRootSignature = root;
  pipelineDesc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
  ID3D12PipelineState *pipeline = nullptr;
  check("pipeline", device->CreateComputePipelineState(
                        &pipelineDesc, IID_PPV_ARGS(&pipeline)));

  auto uploadHeap = heap(D3D12_HEAP_TYPE_UPLOAD);
  auto defaultHeap = heap(D3D12_HEAP_TYPE_DEFAULT);
  auto readbackHeap = heap(D3D12_HEAP_TYPE_READBACK);
  auto create = [&](UINT64 size, D3D12_HEAP_PROPERTIES *properties,
                    D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    ID3D12Resource *resource = nullptr;
    auto desc = buffer(size, flags);
    check("resource", device->CreateCommittedResource(
                          properties, D3D12_HEAP_FLAG_NONE, &desc, state,
                          nullptr, IID_PPV_ARGS(&resource)));
    return resource;
  };
  ID3D12Resource *weightResource = create(
      matrix.size(), &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *inputResource = create(
      input.size() * 4, &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *outputResource = create(
      oracle.size() * 4, &defaultHeap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *readback = create(
      oracle.size() * 4, &readbackHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_NONE);
  D3D12_RANGE empty{0, 0};
  void *mapped = nullptr;
  weightResource->Map(0, &empty, &mapped);
  std::memcpy(mapped, matrix.data(), matrix.size());
  weightResource->Unmap(0, nullptr);
  inputResource->Map(0, &empty, &mapped);
  std::memcpy(mapped, input.data(), input.size() * 4);
  inputResource->Unmap(0, nullptr);

  D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3,
      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *descriptorHeap = nullptr;
  check("descriptor_heap", device->CreateDescriptorHeap(
                               &descriptorHeapDesc,
                               IID_PPV_ARGS(&descriptorHeap)));
  UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto descriptor = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.Buffer.NumElements = matrix.size() / 4;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(weightResource, &srv, descriptor);
  descriptor.ptr += descriptorSize;
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  srv.Buffer.NumElements = input.size();
  srv.Buffer.StructureByteStride = 4;
  device->CreateShaderResourceView(inputResource, &srv, descriptor);
  descriptor.ptr += descriptorSize;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = oracle.size();
  uav.Buffer.StructureByteStride = 4;
  device->CreateUnorderedAccessView(outputResource, nullptr, &uav, descriptor);

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  ID3D12CommandQueue *queue = nullptr;
  check("queue", device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
  ID3D12CommandAllocator *allocator = nullptr;
  check("allocator", device->CreateCommandAllocator(
                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                         IID_PPV_ARGS(&allocator)));
  ID3D12GraphicsCommandList *commands = nullptr;
  check("commands", device->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipeline,
                        IID_PPV_ARGS(&commands)));
  ID3D12DescriptorHeap *heaps[] = {descriptorHeap};
  commands->SetDescriptorHeaps(1, heaps);
  commands->SetComputeRootSignature(root);
  commands->SetComputeRootDescriptorTable(
      0, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
  commands->Dispatch(64, 1, 1);
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = outputResource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  commands->ResourceBarrier(1, &barrier);
  commands->CopyBufferRegion(readback, 0, outputResource, 0, oracle.size() * 4);
  check("close", commands->Close());
  LARGE_INTEGER frequency, begin, end;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&begin);
  ID3D12CommandList *lists[] = {commands};
  queue->ExecuteCommandLists(1, lists);
  ID3D12Fence *fence = nullptr;
  check("fence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS(&fence)));
  check("signal", queue->Signal(fence, 1));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  check("event", fence->SetEventOnCompletion(1, event));
  WaitForSingleObject(event, INFINITE);
  QueryPerformanceCounter(&end);
  D3D12_RANGE all{0, oracle.size() * 4};
  readback->Map(0, &all, &mapped);
  const float *result = reinterpret_cast<const float *>(mapped);
  double absolute = 0, squared = 0;
  size_t exact = 0;
  for (size_t i = 0; i < oracle.size(); ++i) {
    const double difference = result[i] - oracle[i];
    absolute += std::fabs(difference);
    squared += difference * difference;
    exact += result[i] == oracle[i];
  }
  std::printf("submit_to_fence_ms: %.3f\nMAE: %.9f\nRMSE: %.9f\nexact: %.6f\n",
              1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart,
              absolute / oracle.size(), std::sqrt(squared / oracle.size()),
              static_cast<double>(exact) / oracle.size());
  return 0;
}
