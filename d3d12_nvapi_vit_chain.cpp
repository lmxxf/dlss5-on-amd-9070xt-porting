#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <fstream>
#include <utility>
#include <vector>
#include <windows.h>
using NvU32 = uint32_t;
using NvU64 = uint64_t;
using NvAPI_Status = int;
using NVDX_ObjectHandle = void *;
constexpr int NVAPI_OK = 0;
struct DIM3 {
  NvU32 x, y, z;
};
struct KP {
  NVDX_ObjectHandle hFunction;
  DIM3 gridDim, blockDim;
  NvU32 dynSharedMemBytes;
  const void *pParams;
  NvU32 paramSize;
};
using Query = void *(__cdecl *)(unsigned);
static Query qi() {
  static Query q = (Query)GetProcAddress(LoadLibraryW(L"nvapi64.dll"),
                                         "nvapi_QueryInterface");
  return q;
}
static int initnv() {
  using F = int(__cdecl *)();
  return ((F)qi()(0x0150E828))();
}
static int mkmod(ID3D12Device *d, const void *b, NvU32 n, void **h) {
  using F = int(__cdecl *)(ID3D12Device *, const void *, NvU32, void **);
  return ((F)qi()(0xad1a677d))(d, b, n, h);
}
static int mkfn(ID3D12Device *d, void *m, const char *n, void **h) {
  using F = int(__cdecl *)(ID3D12Device *, void *, const char *, void **);
  return ((F)qi()(0xe2436e22))(d, m, n, h);
}
static int chain(ID3D12GraphicsCommandList *c, const KP *p, NvU32 n) {
  struct X {
    NVDX_ObjectHandle hFunction;
    DIM3 gridDim, blockDim;
    NvU32 dynSharedMemBytes;
    const void *pParams;
    NvU32 paramSize;
    void **kernelParams;
  };
  using F = int(__cdecl *)(ID3D12GraphicsCommandList *, const X *, NvU32);
  F f = (F)qi()(0x846a9bf0);
  std::vector<X> chainParams;
  chainParams.reserve(n);
  for (NvU32 i = 0; i < n; ++i) {
    chainParams.push_back(
        X{p[i].hFunction, p[i].gridDim, p[i].blockDim,
          p[i].dynSharedMemBytes, p[i].pParams, p[i].paramSize, nullptr});
  }
  return f(c, chainParams.data(), n);
}
static void hr(const char *n, HRESULT x) {
  if (FAILED(x)) {
    std::fprintf(stderr, "%s=%08lx\n", n, x);
    ExitProcess(1);
  }
}
static void nv(const char *n, int x) {
  if (x) {
    std::fprintf(stderr, "%s=%d\n", n, x);
    ExitProcess(1);
  }
}
static std::vector<uint8_t> rd(const wchar_t *p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f)
    ExitProcess(2);
  size_t n = (size_t)f.tellg();
  f.seekg(0);
  std::vector<uint8_t> x(n);
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
static void p64(std::vector<uint8_t> &b, size_t o, uint64_t v) {
  std::memcpy(b.data() + o, &v, 8);
}
struct W {
  uint64_t e, c, q, p;
};
int wmain(int ac, wchar_t **av) {
  if (ac < 5 || ac > 8) {
    std::fwprintf(stderr, L"usage: %ls cubin input weights output [limit [result-index [single-block]]]\n", av[0]);
    return 2;
  }
  constexpr UINT64 A = 2097152, WA = 147719680;
  auto cubin = rd(av[1]), input = rd(av[2]), weights = rd(av[3]);
  if (input.size() != A || weights.size() != WA)
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
  ID3D12Fence *fence = nullptr;
  hr("fence", dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                IID_PPV_ARGS(&fence)));
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  UINT64 fenceValue = 0;
  UINT submitIndex = 0;
  auto submitAndReset = [&]() {
    std::fprintf(stderr, "submit[%u]\n", submitIndex++);
    hr("close", cl->Close());
    ID3D12CommandList *lists[] = {cl};
    q->ExecuteCommandLists(1, lists);
    hr("signal", q->Signal(fence, ++fenceValue));
    hr("fence_event", fence->SetEventOnCompletion(fenceValue, ev));
    WaitForSingleObject(ev, INFINITE);
    cl->Release();
    ca->Release();
    ca = nullptr;
    cl = nullptr;
    HRESULT allocatorResult = dev->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca));
    if (FAILED(allocatorResult)) {
      std::fprintf(stderr, "allocator_next=%08lx removed=%08lx\n",
                   allocatorResult, dev->GetDeviceRemovedReason());
      ExitProcess(1);
    }
    hr("list_next",
       dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, nullptr,
                              IID_PPV_ARGS(&cl)));
  };
  auto uph = hp(D3D12_HEAP_TYPE_UPLOAD), defh = hp(D3D12_HEAP_TYPE_DEFAULT),
       rbh = hp(D3D12_HEAP_TYPE_READBACK);
  auto make = [&](UINT64 n, D3D12_HEAP_PROPERTIES *h, D3D12_RESOURCE_STATES s,
                  D3D12_RESOURCE_FLAGS f) {
    ID3D12Resource *r = nullptr;
    auto d = bd(n, f);
    hr("resource", dev->CreateCommittedResource(h, D3D12_HEAP_FLAG_NONE, &d, s,
                                                nullptr, IID_PPV_ARGS(&r)));
    return r;
  };
  ID3D12Resource *upIn = make(A, &uph, D3D12_RESOURCE_STATE_GENERIC_READ,
                              D3D12_RESOURCE_FLAG_NONE),
                 *upW = make(WA, &uph, D3D12_RESOURCE_STATE_GENERIC_READ,
                             D3D12_RESOURCE_FLAG_NONE),
                 *zero = make(A, &uph, D3D12_RESOURCE_STATE_GENERIC_READ,
                              D3D12_RESOURCE_FLAG_NONE),
                 *rb = make(A * 11, &rbh, D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_FLAG_NONE);
  void *p = nullptr;
  D3D12_RANGE z{0, 0};
  upIn->Map(0, &z, &p);
  std::memcpy(p, input.data(), A);
  upIn->Unmap(0, nullptr);
  upW->Map(0, &z, &p);
  std::memcpy(p, weights.data(), WA);
  upW->Unmap(0, nullptr);
  zero->Map(0, &z, &p);
  std::memset(p, 0, A);
  zero->Unmap(0, nullptr);
  std::vector<ID3D12Resource *> r(11);
  for (auto &x : r)
    x = make(A, &defh, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  std::vector<ID3D12Resource *> extraScratch(7 * 7);
  for (auto &x : extraScratch)
    x = make(A, &defh, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource *wr = make(WA, &defh, D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  for (auto *x : r)
    cl->CopyBufferRegion(x, 0, zero, 0, A);
  for (auto *x : extraScratch)
    cl->CopyBufferRegion(x, 0, zero, 0, A);
  cl->CopyBufferRegion(r[0], 0, upIn, 0, A);
  cl->CopyBufferRegion(wr, 0, upW, 0, WA);
  std::vector<D3D12_RESOURCE_BARRIER> bars;
  for (auto *x : r) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = x;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bars.push_back(b);
  }
  for (auto *x : extraScratch) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = x;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bars.push_back(b);
  }
  D3D12_RESOURCE_BARRIER wb{};
  wb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  wb.Transition.pResource = wr;
  wb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  wb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  wb.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  bars.push_back(wb);
  cl->ResourceBarrier((UINT)bars.size(), bars.data());
  submitAndReset();
  nv("init", initnv());
  void *mod = nullptr;
  nv("module", mkmod(dev, cubin.data(), (NvU32)cubin.size(), &mod));
  const char *names[] = {
      "cc_vit_1d_repack_2d_to_1d_fp8", "cc_vit_1d_ffn_expand_fp8",
      "cc_vit_1d_ffn_contract_fp8",    "cc_vit_1d_qkv_fp8",
      "cc_vit_1d_attention_fp8",       "cc_vit_1d_projection_fp8",
      "cc_vit_1d_repack_1d_to_2d_fp8",
      "cc_vit_1d_projection_wait_fp8"};
  void *fn[8]{};
  for (int i = 0; i < 8; ++i)
    nv(names[i], mkfn(dev, mod, names[i], &fn[i]));
  const char *chainedNames[] = {
      "cc_vit_1d_ffn_expand_chained_fp8",
      "cc_vit_1d_ffn_contract_chained_fp8",
      "cc_vit_1d_qkv_chained_fp8",
      "cc_vit_1d_attention_chained_fp8",
      "cc_vit_1d_projection_chained_fp8"};
  void *chainedFn[5]{};
  for (int i = 0; i < 5; ++i)
    nv(chainedNames[i], mkfn(dev, mod, chainedNames[i], &chainedFn[i]));
  const W ws[] = {{0x15f6200, 0x19f6400, 0x1df6c00, 0x20f7000},
                  {0x21f7800, 0x25f7a00, 0x29f8200, 0x2cf8600},
                  {0x2df8e00, 0x31f9000, 0x35f9800, 0x38f9c00},
                  {0x39fa400, 0x3dfa600, 0x41fae00, 0x44fb200},
                  {0x45fba00, 0x49fbc00, 0x4dfc400, 0x50fc800},
                  {0x51fd000, 0x55fd200, 0x59fda00, 0x5cfde00},
                  {0x5dfe600, 0x61fe800, 0x65ff000, 0x68ff400},
                  {0x69ffc00, 0x6dffe00, 0x7200600, 0x7500a00}};
  auto va = [&](int i) { return r[i]->GetGPUVirtualAddress(); };
  uint64_t wva = wr->GetGPUVirtualAddress(), cur = va(0), next = va(1),
           branch = va(2), mainv = va(3), attn = va(4), work = va(5),
           aux = va(6), qv[3] = {va(7), va(8), va(9)}, out2d = va(10),
           dims = 8ull | (8ull << 32), tokens = 64;
  std::vector<std::vector<uint8_t>> blobs;
  std::vector<KP> ks;
  std::vector<size_t> groupEnds;
  // Every KP stores a raw pointer into its blob. Reallocation here turns the
  // already-built NVAPI chain into dangling parameter pointers and can wedge
  // WDDM, so reserve above the complete 82-kernel chain before the first add.
  blobs.reserve(96);
  ks.reserve(96);
  auto add = [&](void *f, DIM3 g, DIM3 b, std::vector<uint8_t> &&blob) {
    blobs.push_back(std::move(blob));
    KP k{};
    k.hFunction = f;
    k.gridDim = g;
    k.blockDim = b;
    k.pParams = blobs.back().data();
    k.paramSize = (NvU32)blobs.back().size();
    ks.push_back(k);
  };
  int eight = 8;
  const int singleBlock = ac >= 8 ? _wtoi(av[7]) : -1;
  const bool singleMode = singleBlock >= 31 && singleBlock <= 38;
  if (!singleMode) {
    std::vector<uint8_t> rp(0x18);
    p64(rp, 0, cur);
    p64(rp, 8, next);
    std::memcpy(rp.data() + 16, &eight, 4);
    std::memcpy(rp.data() + 20, &eight, 4);
    add(fn[0], {80, 1, 1}, {32, 4, 1}, std::move(rp));
    groupEnds.push_back(ks.size());
    cur = next;
    next = va(0);
  }
  const int beginBlock = singleMode ? singleBlock - 31 : 0;
  const int endBlock = singleMode ? beginBlock + 1 : 8;
  for (int bi = beginBlock; bi < endBlock; ++bi) {
    uint64_t blockBranch = branch, blockMain = mainv, blockAttn = attn,
             blockWork = work, blockQ[3] = {qv[0], qv[1], qv[2]};
    if (bi > 0) {
      const size_t si = static_cast<size_t>(bi - 1) * 7;
      blockBranch = extraScratch[si + 0]->GetGPUVirtualAddress();
      blockMain = extraScratch[si + 1]->GetGPUVirtualAddress();
      blockAttn = extraScratch[si + 2]->GetGPUVirtualAddress();
      blockWork = extraScratch[si + 3]->GetGPUVirtualAddress();
      for (int qi = 0; qi < 3; ++qi)
        blockQ[qi] = extraScratch[si + 4 + qi]->GetGPUVirtualAddress();
    }
    uint64_t base = aux + bi * 0x3000, prev = bi ? base - 0x400 : 0,
             a1 = base + 0xa00, a2 = base + 0xe00, a3 = base + 0x1200,
             a4 = base + 0x1800, a5 = base + 0x1e00, a20 = base + 0x2800,
             a38 = base + 0x2c00;
    std::vector<uint8_t> e(0x48);
    p64(e, 0, cur);
    p64(e, 16, blockBranch);
    p64(e, 24, wva + ws[bi].e);
    p64(e, 48, prev);
    p64(e, 56, base);
    std::memcpy(e.data() + 64, &tokens, 8);
    add(bi == 0 ? fn[1] : chainedFn[0], {32, 1, 1}, {32, 4, 1},
        std::move(e));
    groupEnds.push_back(ks.size());
    std::vector<uint8_t> c(0x48);
    p64(c, 0, blockBranch);
    p64(c, 8, cur);
    p64(c, 16, blockMain);
    p64(c, 24, wva + ws[bi].c);
    p64(c, 32, a1);
    p64(c, 40, blockWork);
    p64(c, 48, base);
    p64(c, 56, a2);
    std::memcpy(c.data() + 64, &tokens, 8);
    add(fn[2], {8, 1, 4}, {32, 4, 1}, std::move(c));
    std::vector<uint8_t> c2 = blobs.back();
    add(chainedFn[1], {8, 1, 4}, {32, 4, 1}, std::move(c2));
    groupEnds.push_back(ks.size());
    std::vector<uint8_t> qq(0x50);
    p64(qq, 0, blockMain);
    p64(qq, 8, blockQ[0]);
    p64(qq, 16, blockQ[1]);
    p64(qq, 24, blockQ[2]);
    p64(qq, 32, wva + ws[bi].q);
    p64(qq, 40, a3);
    p64(qq, 48, blockWork);
    p64(qq, 56, a2);
    p64(qq, 64, a4);
    std::memcpy(qq.data() + 72, &dims, 8);
    std::vector<uint8_t> qq2 = qq;
    add(fn[3], {16, 1, 2}, {32, 4, 1}, std::move(qq));
    groupEnds.push_back(ks.size());
    std::vector<uint8_t> a(0x40);
    p64(a, 0, blockQ[0]);
    p64(a, 8, blockQ[1]);
    p64(a, 16, blockQ[2]);
    p64(a, 24, blockAttn);
    p64(a, 32, blockWork);
    p64(a, 40, a4);
    p64(a, 48, a5);
    std::memcpy(a.data() + 56, &dims, 8);
    add(fn[4], {32, 1, 1}, {32, 4, 1}, std::move(a));
    groupEnds.push_back(ks.size());
    std::vector<uint8_t> pr(0x48);
    p64(pr, 0, blockAttn);
    p64(pr, 8, blockMain);
    p64(pr, 16, next);
    p64(pr, 24, wva + ws[bi].p);
    p64(pr, 32, a20);
    p64(pr, 40, blockWork);
    p64(pr, 48, a5);
    p64(pr, 56, a38);
    std::memcpy(pr.data() + 64, &dims, 8);
    std::vector<uint8_t> pr2 = pr;
    std::vector<uint8_t> pr3 = pr;
    add(fn[5], {8, 1, 4}, {32, 4, 1}, std::move(pr));
    groupEnds.push_back(ks.size());
    std::swap(cur, next);
  }
  if (!singleMode) {
    std::vector<uint8_t> ro(0x18);
    p64(ro, 0, cur);
    p64(ro, 8, out2d);
    std::memcpy(ro.data() + 16, &eight, 4);
    std::memcpy(ro.data() + 20, &eight, 4);
    add(fn[6], {64, 1, 1}, {32, 4, 1}, std::move(ro));
    groupEnds.push_back(ks.size());
  }
  NvU32 limit = singleMode ? static_cast<NvU32>(ks.size())
                           : (ac >= 6 ? (NvU32)_wtoi(av[5])
                                      : (NvU32)ks.size());
  if (limit == 0 || limit > ks.size())
    return 2;
  size_t groupStart = 0;
  bool foundBoundary = false;
  for (size_t groupEnd : groupEnds) {
    if (groupEnd > limit)
      break;
    nv("chain", chain(cl, ks.data() + groupStart,
                      static_cast<NvU32>(groupEnd - groupStart)));
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    cl->ResourceBarrier(1, &uav);
    submitAndReset();
    groupStart = groupEnd;
    foundBoundary = groupEnd == limit;
  }
  if (!foundBoundary)
    return 2;
  int resultIndex = 10;
  if (limit == 1) resultIndex = 1;
  else if (limit == 2) resultIndex = 2;
  else if (limit == 3) resultIndex = 3;
  else if (limit == 4) resultIndex = 7;
  else if (limit == 5) resultIndex = 4;
  else if (limit == 6) resultIndex = 0;
  if (ac == 7) resultIndex = _wtoi(av[6]);
  if (singleMode) resultIndex = ac >= 7 ? _wtoi(av[6]) : 1;
  const bool dumpAll = resultIndex == 99;
  if (!dumpAll && (resultIndex < 0 || resultIndex >= (int)r.size())) return 2;
  if (dumpAll) {
    std::vector<D3D12_RESOURCE_BARRIER> obs;
    for (auto *resource : r) {
      D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = resource; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      obs.push_back(b);
    }
    cl->ResourceBarrier((UINT)obs.size(), obs.data());
    for (size_t i=0;i<r.size();++i) cl->CopyBufferRegion(rb, i*A, r[i], 0, A);
  } else {
    std::vector<D3D12_RESOURCE_BARRIER> obs;
    for (auto *resource : r) {
      D3D12_RESOURCE_BARRIER b{};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = resource;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      obs.push_back(b);
    }
    cl->ResourceBarrier((UINT)obs.size(), obs.data());
    cl->CopyBufferRegion(rb, 0, r[resultIndex], 0, A);
  }
  hr("close", cl->Close());
  ID3D12CommandList *ls[] = {cl};
  q->ExecuteCommandLists(1, ls);
  q->Signal(fence, ++fenceValue);
  fence->SetEventOnCompletion(fenceValue, ev);
  WaitForSingleObject(ev, INFINITE);
  const UINT64 readBytes = dumpAll ? A*11 : A;
  D3D12_RANGE all{0, readBytes};
  rb->Map(0, &all, &p);
  std::vector<uint8_t> got(readBytes);
  std::memcpy(got.data(), p, readBytes);
  rb->Unmap(0, nullptr);
  size_t nz = 0, nan = 0, last = 0;
  for (size_t i = 0; i < readBytes; ++i) {
    if (got[i]) {
      ++nz;
      last = i;
    }
    nan += got[i] == 0x7f || got[i] == 0xff;
  }
  std::printf("adapter=%ls kernels=%u nonzero=%zu last=%zu nan=%zu\n",
              dd.Description, limit, nz, last, nan);
  std::ofstream(av[4], std::ios::binary).write((char *)got.data(), got.size());
  return nan ? 3 : 0;
}
