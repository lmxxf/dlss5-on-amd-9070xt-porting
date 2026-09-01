#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <fstream>
#include <vector>
#include <windows.h>
using NvU32 = uint32_t;
using NvU64 = uint64_t;
using NvAPI_Status = int;
using NVDX_ObjectHandle = void *;
constexpr int NVAPI_OK = 0;
struct NVAPI_DIM3 {
  NvU32 x, y, z;
};
struct NVAPI_CU_KERNEL_LAUNCH_PARAMS {
  NVDX_ObjectHandle hFunction;
  NVAPI_DIM3 gridDim, blockDim;
  NvU32 dynSharedMemBytes;
  void const *pParams;
  NvU32 paramSize;
};
using Query = void *(__cdecl *)(unsigned);
static Query qi() {
  static Query q = (Query)GetProcAddress(LoadLibraryW(L"nvapi64.dll"),
                                         "nvapi_QueryInterface");
  return q;
}
static NvAPI_Status NvAPI_Initialize() {
  using F = NvAPI_Status(__cdecl *)();
  return ((F)qi()(0x0150E828))();
}
static NvAPI_Status NvAPI_D3D12_CreateCuModule(ID3D12Device *d, const void *b,
                                               NvU32 n, NVDX_ObjectHandle *h) {
  using F = NvAPI_Status(__cdecl *)(ID3D12Device *, const void *, NvU32,
                                    NVDX_ObjectHandle *);
  return ((F)qi()(0xad1a677d))(d, b, n, h);
}
static NvAPI_Status NvAPI_D3D12_CreateCuFunction(ID3D12Device *d,
                                                 NVDX_ObjectHandle m,
                                                 const char *n,
                                                 NVDX_ObjectHandle *h) {
  using F = NvAPI_Status(__cdecl *)(ID3D12Device *, NVDX_ObjectHandle,
                                    const char *, NVDX_ObjectHandle *);
  return ((F)qi()(0xe2436e22))(d, m, n, h);
}
static NvAPI_Status
NvAPI_D3D12_LaunchCuKernelChain(ID3D12GraphicsCommandList *c,
                                const NVAPI_CU_KERNEL_LAUNCH_PARAMS *p,
                                NvU32 n) {
  using F =
      NvAPI_Status(__cdecl *)(ID3D12GraphicsCommandList *,
                              const NVAPI_CU_KERNEL_LAUNCH_PARAMS *, NvU32);
  return ((F)qi()(0x24973538))(c, p, n);
}
static void hr(const char *n, HRESULT x) {
  if (FAILED(x)) {
    std::fprintf(stderr, "%s=%08lx\n", n, x);
    ExitProcess(1);
  }
}
static void nv(const char *n, NvAPI_Status x) {
  if (x != NVAPI_OK) {
    std::fprintf(stderr, "%s=%d\n", n, x);
    ExitProcess(1);
  }
}
static std::vector<unsigned char> rd(const wchar_t *p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f)
    ExitProcess(2);
  size_t n = (size_t)f.tellg();
  f.seekg(0);
  std::vector<unsigned char> x(n);
  f.read((char *)x.data(), n);
  return x;
}
static D3D12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE t) {
  D3D12_HEAP_PROPERTIES h{};
  h.Type = t;
  h.CreationNodeMask = h.VisibleNodeMask = 1;
  return h;
}
static D3D12_RESOURCE_DESC
bd(UINT64 n, D3D12_RESOURCE_FLAGS f = D3D12_RESOURCE_FLAG_NONE) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = n;
  d.Height = 1;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  d.Flags = f;
  return d;
}
int wmain(int ac, wchar_t **av) {
  if (ac != 5) {
    std::fwprintf(stderr, L"usage: %ls cubin input expected output\n", av[0]);
    return 2;
  }
  auto cubin = rd(av[1]), input = rd(av[2]), expected = rd(av[3]);
  constexpr UINT64 N = 2097152;
  if (input.size() != N || expected.size() != N)
    return 2;
  IDXGIFactory6 *fac = nullptr;
  hr("factory", CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)));
  IDXGIAdapter1 *ad = nullptr;
  DXGI_ADAPTER_DESC1 dd{};
  for (UINT i = 0;; ++i) {
    IDXGIAdapter1 *c = nullptr;
    if (fac->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                        IID_PPV_ARGS(&c)) ==
        DXGI_ERROR_NOT_FOUND)
      break;
    DXGI_ADAPTER_DESC1 d{};
    c->GetDesc1(&d);
    if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        wcsstr(d.Description, L"NVIDIA")) {
      ad = c;
      dd = d;
      break;
    }
    c->Release();
  }
  if (!ad)
    return 1;
  ID3D12Device *dev = nullptr;
  hr("device",
     D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)));
  D3D12_COMMAND_QUEUE_DESC qd{};
  ID3D12CommandQueue *q = nullptr;
  hr("queue", dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)));
  ID3D12CommandAllocator *ca = nullptr;
  hr("allocator", dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&ca)));
  ID3D12GraphicsCommandList *cl = nullptr;
  hr("list", dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca,
                                    nullptr, IID_PPV_ARGS(&cl)));
  auto up = hp(D3D12_HEAP_TYPE_UPLOAD), de = hp(D3D12_HEAP_TYPE_DEFAULT),
       rbh = hp(D3D12_HEAP_TYPE_READBACK);
  ID3D12Resource *upload, *in, *out, *readback;
  auto desc = bd(N, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), plain = bd(N);
  hr("upload", dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &plain,
                                            D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr, IID_PPV_ARGS(&upload)));
  hr("in", dev->CreateCommittedResource(&de, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&in)));
  hr("out", dev->CreateCommittedResource(&de, D3D12_HEAP_FLAG_NONE, &desc,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         nullptr, IID_PPV_ARGS(&out)));
  hr("readback",
     dev->CreateCommittedResource(&rbh, D3D12_HEAP_FLAG_NONE, &plain,
                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&readback)));
  void *p = nullptr;
  D3D12_RANGE z{0, 0};
  upload->Map(0, &z, &p);
  std::memcpy(p, input.data(), N);
  upload->Unmap(0, nullptr);
  cl->CopyBufferRegion(in, 0, upload, 0, N);
  D3D12_RESOURCE_BARRIER ib{};
  ib.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  ib.Transition.pResource = in;
  ib.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  ib.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  ib.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  cl->ResourceBarrier(1, &ib);
  nv("NvAPI_Initialize", NvAPI_Initialize());
  NVDX_ObjectHandle mod = nullptr, fn = nullptr;
  nv("CreateCuModule",
     NvAPI_D3D12_CreateCuModule(dev, cubin.data(), (NvU32)cubin.size(), &mod));
  nv("CreateCuFunction", NvAPI_D3D12_CreateCuFunction(
                             dev, mod, "cc_vit_1d_repack_2d_to_1d_fp8", &fn));
  struct P {
    NvU64 in, out;
    NvU32 w, h;
  } params{in->GetGPUVirtualAddress(), out->GetGPUVirtualAddress(), 8, 8};
  NVAPI_CU_KERNEL_LAUNCH_PARAMS kp{};
  kp.hFunction = fn;
  kp.gridDim = {80, 1, 1};
  kp.blockDim = {32, 4, 1};
  kp.pParams = &params;
  kp.paramSize = sizeof(params);
  nv("LaunchChain", NvAPI_D3D12_LaunchCuKernelChain(cl, &kp, 1));
  D3D12_RESOURCE_BARRIER ob{};
  ob.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  ob.Transition.pResource = out;
  ob.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  ob.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ob.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  cl->ResourceBarrier(1, &ob);
  cl->CopyBufferRegion(readback, 0, out, 0, N);
  hr("close", cl->Close());
  ID3D12CommandList *ls[] = {cl};
  q->ExecuteCommandLists(1, ls);
  ID3D12Fence *f = nullptr;
  hr("fence", dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f)));
  q->Signal(f, 1);
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  f->SetEventOnCompletion(1, ev);
  WaitForSingleObject(ev, INFINITE);
  D3D12_RANGE all{0, N};
  readback->Map(0, &all, &p);
  std::vector<unsigned char> got(N);
  std::memcpy(got.data(), p, N);
  readback->Unmap(0, nullptr);
  size_t eq = 0, nz = 0;
  for (size_t i = 0; i < N; ++i) {
    eq += got[i] == expected[i];
    nz += got[i] != 0;
  }
  std::printf("adapter=%ls equal=%zu/%llu nonzero=%zu\n", dd.Description, eq,
              (unsigned long long)N, nz);
  std::ofstream(av[4], std::ios::binary).write((char *)got.data(), got.size());
  return eq == N ? 0 : 3;
}
