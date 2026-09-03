#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
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
static float e4m3(uint8_t value) {
  float sign = value & 128 ? -1.0f : 1.0f;
  unsigned exponent = (value >> 3) & 15, mantissa = value & 7;
  if (!exponent) return sign * (mantissa / 8.0f) * std::ldexp(1.0f, -6);
  if (exponent == 15 && mantissa == 7) return NAN;
  return sign * (1.0f + mantissa / 8.0f) *
         std::ldexp(1.0f, static_cast<int>(exponent) - 7);
}
static size_t append_aligned(std::vector<uint8_t> &destination,
                             const std::vector<uint8_t> &source) {
  while (destination.size() & 3) destination.push_back(0);
  size_t offset = destination.size();
  destination.insert(destination.end(), source.begin(), source.end());
  return offset;
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 11) {
    std::fwprintf(stderr,
        L"usage: %ls expand.f16 contract.f16 contract-skip.f16 "
        L"qkv-main.fp8 qkv-work.f16 projection.f16 projection-skip.f16 "
        L"input.f32 oracle.f32 dump.bin\n", argv[0]);
    return 2;
  }
  auto expand = read_file(argv[1]), contract = read_file(argv[2]),
       contractSkip = read_file(argv[3]), qkvMain = read_file(argv[4]),
       qkvWork = read_file(argv[5]), projection = read_file(argv[6]),
       projectionSkip = read_file(argv[7]), input = read_file(argv[8]),
       oracle = read_file(argv[9]);
  if (expand.size() != 1024ull * 4096 * 2 ||
      contract.size() != 4096ull * 1024 * 2 || contractSkip.size() != 2048 ||
      qkvMain.size() != 1024ull * 3 * 1024 ||
      qkvWork.size() != 1024ull * 3 * 1024 * 2 ||
      projection.size() != 1024ull * 1024 * 2 ||
      projectionSkip.size() != 2048 || input.empty() ||
      input.size() % (1024ull * 4) ||
      oracle.size() != input.size()) return 2;
  const UINT tokens = static_cast<UINT>(input.size() / (1024ull * 4));
  std::vector<uint8_t> packed;
  size_t expandOffset = append_aligned(packed, expand);
  size_t contractOffset = append_aligned(packed, contract);
  size_t contractSkipOffset = append_aligned(packed, contractSkip);
  size_t qkvMainOffset = append_aligned(packed, qkvMain);
  size_t qkvWorkOffset = append_aligned(packed, qkvWork);
  size_t projectionOffset = append_aligned(packed, projection);
  size_t projectionSkipOffset = append_aligned(packed, projectionSkip);
  std::vector<float> scales(64);
  for (int group = 0; group < 2; ++group)
    for (int head = 0; head < 32; ++head) {
      std::vector<float> norms(1024);
      for (int row = 0; row < 1024; ++row) {
        float sum = 0;
        for (int dim = 0; dim < 32; ++dim) {
          size_t index = (row * 3 + group) * 1024 + head * 32 + dim;
          float value = e4m3(qkvMain[index]);
          sum += value * value;
        }
        norms[row] = std::sqrt(sum);
      }
      std::nth_element(norms.begin(), norms.begin() + 512, norms.end());
      scales[group * 32 + head] = norms[512];
    }

  IDXGIFactory6 *factory = nullptr;
  check("factory", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  IDXGIAdapter1 *adapter = nullptr;
  DXGI_ADAPTER_DESC1 adapterDesc{};
  for (UINT i = 0;; ++i) {
    IDXGIAdapter1 *candidate = nullptr;
    if (factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
    candidate->GetDesc1(&adapterDesc);
    if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        wcsstr(adapterDesc.Description, L"AMD")) {
      adapter = candidate;
      break;
    }
    candidate->Release();
  }
  if (!adapter) return 1;
  ID3D12Device *device = nullptr;
  check("device", D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                     IID_PPV_ARGS(&device)));
  std::fwprintf(stderr, L"adapter: %ls\n", adapterDesc.Description);

  const char shader[] = R"(
ByteAddressBuffer weights:register(t0);
StructuredBuffer<float> source:register(t1),scales:register(t2);
RWStructuredBuffer<float> branch:register(u0),hidden:register(u1),qkv:register(u2),attention:register(u3),result:register(u4);
float H(uint base,uint i){uint x=weights.Load(base+(i>>1)*4);return f16tof32((x>>((i&1)*16))&65535);}
float E(uint base,uint i){uint x=weights.Load(base+(i&~3));uint v=(x>>((i&3)*8))&255;float s=(v&128)?-1:1;uint e=(v>>3)&15,m=v&7;if(e==0)return s*(m/8.0)*exp2(-6.0);return s*(1+m/8.0)*exp2((int)e-7);}
float F(float x){if(x==0)return 0;float s=x<0?-1:1,a=abs(x);if(a<.015625)return s*round(a*512)/512;float e=clamp(floor(log2(a)),-6.,8.),m=round((a/exp2(e)-1)*8);if(m>=8){m=0;e+=1;}return s*min(exp2(e)*(1+m/8),448.);}
groupshared float shared_values[64];
[numthreads(64,1,1)]void expand_cs(uint3 id:SV_DispatchThreadID){uint o=id.x;if(o>=TOKENS*1024)return;uint t=o/1024,c=(o%1024)*4;float4 y=0;[loop]for(uint i=0;i<1024;i++){float x=source[t*1024+i];uint w=i*4096+c;y+=x*float4(H(EXPAND_OFFSET,w),H(EXPAND_OFFSET,w+1),H(EXPAND_OFFSET,w+2),H(EXPAND_OFFSET,w+3));}branch[t*4096+c]=F(y.x);branch[t*4096+c+1]=F(y.y);branch[t*4096+c+2]=F(y.z);branch[t*4096+c+3]=F(y.w);}
[numthreads(64,1,1)]void contract_cs(uint3 id:SV_DispatchThreadID,uint3 lane:SV_GroupThreadID){uint o=id.x,t=o/1024,c=o%1024;float y=0;[loop]for(uint base=0;base<4096;base+=64){float x=clamp(branch[t*4096+base+lane.x],-4.,4.);shared_values[lane.x]=x*(.89453125+x*(.447265625-.055908203125*abs(x)));GroupMemoryBarrierWithGroupSync();[unroll]for(uint i=0;i<64;i++)y+=shared_values[i]*H(CONTRACT_OFFSET,(base+i)*1024+c);GroupMemoryBarrierWithGroupSync();}hidden[o]=F(y+source[o]*H(CONTRACT_SKIP_OFFSET,c));}
[numthreads(32,1,1)]void qkv_cs(uint3 tid:SV_GroupThreadID,uint3 gid:SV_GroupID){uint t=gid.x,h=tid.x;float q[32],k[32],v[32];[loop]for(uint d=0;d<32;d++){q[d]=k[d]=v[d]=0;}[loop]for(uint i=0;i<1024;i++){float x=hidden[t*1024+i];[loop]for(uint d=0;d<32;d++){uint o=h*32+d;q[d]+=x*E(QKV_MAIN_OFFSET,(i*3+0)*1024+o);k[d]+=x*E(QKV_MAIN_OFFSET,(i*3+1)*1024+o);v[d]+=x*E(QKV_MAIN_OFFSET,(i*3+2)*1024+o);}}float qn=0,kn=0;[loop]for(uint d=0;d<32;d++){qn+=q[d]*q[d];kn+=k[d]*k[d];}qn=scales[h]*rsqrt(max(qn,1e-12));kn=scales[32+h]*rsqrt(max(kn,1e-12));[loop]for(uint d=0;d<32;d++){uint o=h*32+d;qkv[(t*3+0)*1024+o]=q[d]*qn;qkv[(t*3+1)*1024+o]=k[d]*kn;qkv[(t*3+2)*1024+o]=v[d];}}
[numthreads(64,1,1)]void attention_cs(uint3 id:SV_DispatchThreadID){uint z=id.x;if(z>=TOKENS*32)return;uint t=z/32,h=z%32,base=h*32;float mx=-3.4e38;[loop]for(uint key=0;key<TOKENS;key++){float dot=0;[loop]for(uint d=0;d<32;d++)dot+=qkv[(t*3+0)*1024+base+d]*qkv[(key*3+1)*1024+base+d];mx=max(mx,dot);}float den=0,value[32];[unroll]for(uint d=0;d<32;d++)value[d]=0;[loop]for(uint key=0;key<TOKENS;key++){float dot=0;[loop]for(uint d=0;d<32;d++)dot+=qkv[(t*3+0)*1024+base+d]*qkv[(key*3+1)*1024+base+d];float a=exp(dot-mx);den+=a;[unroll]for(uint d=0;d<32;d++)value[d]+=a*qkv[(key*3+2)*1024+base+d];}[unroll]for(uint d=0;d<32;d++)attention[t*1024+base+d]=F(value[d]/den);}
[numthreads(64,1,1)]void projection_cs(uint3 id:SV_DispatchThreadID){uint o=id.x;if(o>=TOKENS*1024)return;uint t=o/1024,c=o%1024;float y=0;[loop]for(uint i=0;i<1024;i++)y+=attention[t*1024+i]*H(PROJECTION_OFFSET,i*1024+c);result[o]=F(y+hidden[o]*H(PROJECTION_SKIP_OFFSET,c));}
)";
  char offsets[7][32], tokenCount[16];
  size_t offsetValues[7] = {expandOffset, contractOffset, contractSkipOffset,
                            qkvMainOffset, qkvWorkOffset, projectionOffset,
                            projectionSkipOffset};
  for (int i = 0; i < 7; ++i)
    std::snprintf(offsets[i], sizeof(offsets[i]), "%zu", offsetValues[i]);
  std::snprintf(tokenCount, sizeof(tokenCount), "%u", tokens);
  D3D_SHADER_MACRO macros[] = {
      {"EXPAND_OFFSET", offsets[0]}, {"CONTRACT_OFFSET", offsets[1]},
      {"CONTRACT_SKIP_OFFSET", offsets[2]}, {"QKV_MAIN_OFFSET", offsets[3]},
      {"QKV_WORK_OFFSET", offsets[4]}, {"PROJECTION_OFFSET", offsets[5]},
      {"PROJECTION_SKIP_OFFSET", offsets[6]}, {"TOKENS", tokenCount},
      {nullptr, nullptr}};
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, 3};
  D3D12_ROOT_PARAMETER rootParameter{};
  rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameter.DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &rootParameter;
  ID3DBlob *rootBlob = nullptr, *error = nullptr;
  check("serialize", D3D12SerializeRootSignature(
                         &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob,
                         &error));
  ID3D12RootSignature *root = nullptr;
  check("root", device->CreateRootSignature(
                    0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                    IID_PPV_ARGS(&root)));
  const char *entries[5] = {"expand_cs", "contract_cs", "qkv_cs",
                            "attention_cs", "projection_cs"};
  ID3D12PipelineState *pipelines[5]{};
  wchar_t cacheDir[MAX_PATH];
  GetModuleFileNameW(nullptr, cacheDir, MAX_PATH);
  wchar_t *cacheSlash = wcsrchr(cacheDir, L'\\');
  if (cacheSlash) std::wcscpy(cacheSlash + 1, L"shader-cache");
  else std::wcscpy(cacheDir, L"shader-cache");
  CreateDirectoryW(cacheDir, nullptr);
  int cacheHits = 0;
  bool waveAttention = false;
  for (int i = 0; i < 5; ++i) {
    ID3DBlob *code = nullptr;
    if (i == 3) {
      wchar_t modulePath[MAX_PATH], *moduleSlash = nullptr;
      GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
      moduleSlash = wcsrchr(modulePath, L'\\');
      if (moduleSlash) std::wcscpy(moduleSlash + 1, L"vit-attention-wave32.cso");
      if (SUCCEEDED(D3DReadFileToBlob(modulePath, &code))) waveAttention = true;
    }
    wchar_t cachePath[MAX_PATH];
    std::swprintf(cachePath, MAX_PATH, L"%ls\\vit-v5-t%u-%hs.cso",
                  cacheDir, tokens, entries[i]);
    if (code) ++cacheHits;
    else if (SUCCEEDED(D3DReadFileToBlob(cachePath, &code))) ++cacheHits;
    else {
      HRESULT compiled = D3DCompile(shader, sizeof(shader) - 1, nullptr, macros,
                                    nullptr, entries[i], "cs_5_1",
                                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                                    &error);
      if (error) {
        std::fwrite(error->GetBufferPointer(), 1, error->GetBufferSize(), stderr);
        error->Release();
        error = nullptr;
      }
      check(entries[i], compiled);
      check("shader_cache_write", D3DWriteBlobToFile(code, cachePath, TRUE));
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root;
    desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    check("pipeline", device->CreateComputePipelineState(
                          &desc, IID_PPV_ARGS(&pipelines[i])));
    code->Release();
  }
  std::printf("shader_cache_hits=%d/5\n", cacheHits);

  auto uploadHeap = heap(D3D12_HEAP_TYPE_UPLOAD);
  auto defaultHeap = heap(D3D12_HEAP_TYPE_DEFAULT);
  auto readbackHeap = heap(D3D12_HEAP_TYPE_READBACK);
  auto make = [&](UINT64 size, D3D12_HEAP_PROPERTIES *properties,
                  D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    ID3D12Resource *resource = nullptr;
    auto desc = buffer(size, flags);
    check("resource", device->CreateCommittedResource(
                          properties, D3D12_HEAP_FLAG_NONE, &desc, state,
                          nullptr, IID_PPV_ARGS(&resource)));
    return resource;
  };
  ID3D12Resource *weightResource = make(
      packed.size(), &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *inputResource = make(
      input.size(), &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12Resource *scaleResource = make(
      scales.size() * 4, &uploadHeap, D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_FLAG_NONE);
  void *mapped = nullptr;
  D3D12_RANGE empty{0, 0};
  for (auto item : {std::pair<ID3D12Resource *, std::pair<const void *, size_t>>{
                        weightResource, {packed.data(), packed.size()}},
                    {inputResource, {input.data(), input.size()}},
                    {scaleResource, {scales.data(), scales.size() * 4}}}) {
    item.first->Map(0, &empty, &mapped);
    std::memcpy(mapped, item.second.first, item.second.second);
    item.first->Unmap(0, nullptr);
  }
  const UINT64 sizes[5] = {UINT64(tokens) * 4096 * 4, UINT64(tokens) * 1024 * 4,
                           UINT64(tokens) * 3 * 1024 * 4, UINT64(tokens) * 1024 * 4,
                           UINT64(tokens) * 1024 * 4};
  ID3D12Resource *uavs[5]{};
  for (int i = 0; i < 5; ++i)
    uavs[i] = make(sizes[i], &defaultHeap,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  const UINT64 readbackSize = sizes[4];
  ID3D12Resource *readback = make(
      readbackSize, &readbackHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_NONE);
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8,
      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ID3D12DescriptorHeap *descriptorHeap = nullptr;
  check("descriptor_heap", device->CreateDescriptorHeap(
                               &heapDesc, IID_PPV_ARGS(&descriptorHeap)));
  UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto descriptor = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_SHADER_RESOURCE_VIEW_DESC rawView{};
  rawView.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  rawView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  rawView.Format = DXGI_FORMAT_R32_TYPELESS;
  rawView.Buffer.NumElements = static_cast<UINT>(packed.size() / 4);
  rawView.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(weightResource, &rawView, descriptor);
  descriptor.ptr += descriptorSize;
  for (auto pair : {std::pair<ID3D12Resource *, UINT>{inputResource, tokens * 1024},
                    {scaleResource, 64}}) {
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.Buffer.NumElements = pair.second;
    view.Buffer.StructureByteStride = 4;
    device->CreateShaderResourceView(pair.first, &view, descriptor);
    descriptor.ptr += descriptorSize;
  }
  for (int i = 0; i < 5; ++i) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = static_cast<UINT>(sizes[i] / 4);
    view.Buffer.StructureByteStride = 4;
    device->CreateUnorderedAccessView(uavs[i], nullptr, &view, descriptor);
    descriptor.ptr += descriptorSize;
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  ID3D12CommandQueue *queue = nullptr;
  check("queue", device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
  D3D12_QUERY_HEAP_DESC queryDesc{};
  queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  queryDesc.Count = 6;
  ID3D12QueryHeap *queryHeap = nullptr;
  check("query_heap", device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queryHeap)));
  ID3D12Resource *timestampReadback = make(
      6 * sizeof(UINT64), &readbackHeap, D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_FLAG_NONE);
  ID3D12CommandAllocator *allocator = nullptr;
  check("allocator", device->CreateCommandAllocator(
                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                         IID_PPV_ARGS(&allocator)));
  ID3D12GraphicsCommandList *commands = nullptr;
  check("commands", device->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                        IID_PPV_ARGS(&commands)));
  ID3D12DescriptorHeap *heaps[] = {descriptorHeap};
  commands->SetDescriptorHeaps(1, heaps);
  commands->SetComputeRootSignature(root);
  commands->SetComputeRootDescriptorTable(
      0, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
  const UINT dispatches[5] = {tokens * 16, tokens * 16, tokens,
                              (tokens * 32 + 63) / 64, tokens * 16};
  commands->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
  for (int pass = 0; pass < 5; ++pass) {
    commands->SetPipelineState(pipelines[pass]);
    if (pass == 3 && waveAttention) commands->Dispatch(tokens, 32, 1);
    else commands->Dispatch(dispatches[pass], 1, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = uavs[pass];
    commands->ResourceBarrier(1, &barrier);
    commands->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, pass + 1);
  }
  commands->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 6,
                             timestampReadback, 0);
  D3D12_RESOURCE_BARRIER copy{};
  copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  copy.Transition.pResource = uavs[4];
  copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  commands->ResourceBarrier(1, &copy);
  commands->CopyBufferRegion(readback, 0, uavs[4], 0, sizes[4]);
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
  UINT64 timestampFrequency = 0;
  check("timestamp_frequency", queue->GetTimestampFrequency(&timestampFrequency));
  UINT64 *timestamps = nullptr;
  D3D12_RANGE timestampRange{0, 6 * sizeof(UINT64)};
  timestampReadback->Map(0, &timestampRange,
                         reinterpret_cast<void **>(&timestamps));
  D3D12_RANGE range{0, static_cast<SIZE_T>(readbackSize)};
  readback->Map(0, &range, &mapped);
  auto *output = reinterpret_cast<const float *>(mapped);
  std::ofstream(argv[10], std::ios::binary).write(
      reinterpret_cast<const char *>(output), sizes[4]);
  auto *wanted = reinterpret_cast<const float *>(oracle.data());
  double ae = 0, se = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  for (UINT i = 0; i < tokens * 1024; ++i) {
    double x = output[i], y = wanted[i], delta = x - y;
    ae += std::abs(delta); se += delta * delta;
    sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
  }
  double count = double(tokens) * 1024;
  double correlation = (sxy - sx * sy / count) /
      std::sqrt((sxx - sx * sx / count) * (syy - sy * sy / count));
  std::printf("corr=%.9f MAE=%.9f RMSE=%.9f submit_to_fence_ms=%.3f\n",
              correlation, ae / count, std::sqrt(se / count),
              1000.0 * (end.QuadPart - begin.QuadPart) / frequency.QuadPart);
  const char *passNames[5] = {"expand", "contract", "qkv", "attention", "projection"};
  for (int pass = 0; pass < 5; ++pass)
    std::printf("%s_ms=%.3f\n", passNames[pass],
                1000.0 * double(timestamps[pass + 1] - timestamps[pass]) /
                    double(timestampFrequency));
  return 0;
}
