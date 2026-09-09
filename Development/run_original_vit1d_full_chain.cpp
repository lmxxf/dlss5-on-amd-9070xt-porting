#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda.h>
#include <fstream>
#include <utility>
#include <vector>
static void ck_impl(const char *n, CUresult r) {
  if (r) {
    const char *s = nullptr;
    cuGetErrorString(r, &s);
    std::fprintf(stderr, "%s=%d %s\n", n, r, s ? s : "?");
    std::exit(1);
  }
}
#define ck(name, expr)                                                         \
  (std::fprintf(stderr, "BEGIN %s\n", name), std::fflush(stderr),              \
   ck_impl(name, (expr)))
static std::vector<unsigned char> rd(const char *p, size_t n) {
  std::vector<unsigned char> x(n);
  std::ifstream f(p, std::ios::binary);
  if (!f)
    std::exit(2);
  f.read((char *)x.data(), n);
  return x;
}
static void p64(unsigned char *p, size_t o, CUdeviceptr v) {
  std::memcpy(p + o, &v, 8);
}
static void cluster(CUlaunchConfig &c, CUlaunchAttribute *a, unsigned gx,
                    unsigned z) {
  a[0] = {};
  a[0].id = CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;
  a[0].value.clusterDim.x = 1;
  a[0].value.clusterDim.y = 1;
  a[0].value.clusterDim.z = z;
  c = {};
  c.gridDimX = gx;
  c.gridDimY = 1;
  c.gridDimZ = z;
  c.blockDimX = 32;
  c.blockDimY = 4;
  c.blockDimZ = 1;
  c.attrs = a;
  c.numAttrs = 1;
}
struct W {
  uint64_t e, c, q, p;
};
int main(int ac, char **av) {
  if (ac != 4) {
    std::fprintf(stderr, "usage: %s block30-2d output-2d arena\n", av[0]);
    return 2;
  }
  constexpr size_t A = 2097152, WA = 147719680;
  const W ws[] = {{0x15f6200, 0x19f6400, 0x1df6c00, 0x20f7000},
                  {0x21f7800, 0x25f7a00, 0x29f8200, 0x2cf8600},
                  {0x2df8e00, 0x31f9000, 0x35f9800, 0x38f9c00},
                  {0x39fa400, 0x3dfa600, 0x41fae00, 0x44fb200},
                  {0x45fba00, 0x49fbc00, 0x4dfc400, 0x50fc800},
                  {0x51fd000, 0x55fd200, 0x59fda00, 0x5cfde00},
                  {0x5dfe600, 0x61fe800, 0x65ff000, 0x68ff400},
                  {0x69ffc00, 0x6dffe00, 0x7200600, 0x7500a00}};
  auto input = rd(av[1], A), weights = rd(av[3], WA);
  ck("init", cuInit(0));
  CUdevice d;
  ck("dev", cuDeviceGet(&d, 0));
  CUcontext cx;
  ck("ctx", cuDevicePrimaryCtxRetain(&cx, d));
  ck("set", cuCtxSetCurrent(cx));
  CUmodule m;
  ck("module", cuModuleLoad(&m, "/tmp/dlssnr-cubins/dlssnr-05.cubin"));
  CUfunction r21, r12, fe, fc, fq, fa, fp;
  ck("r21", cuModuleGetFunction(&r21, m, "cc_vit_1d_repack_2d_to_1d_fp8"));
  ck("r12", cuModuleGetFunction(&r12, m, "cc_vit_1d_repack_1d_to_2d_fp8"));
  ck("fe", cuModuleGetFunction(&fe, m, "cc_vit_1d_ffn_expand_fp8"));
  ck("fc", cuModuleGetFunction(&fc, m, "cc_vit_1d_ffn_contract_fp8"));
  ck("fq", cuModuleGetFunction(&fq, m, "cc_vit_1d_qkv_fp8"));
  ck("fa", cuModuleGetFunction(&fa, m, "cc_vit_1d_attention_fp8"));
  ck("fp", cuModuleGetFunction(&fp, m, "cc_vit_1d_projection_fp8"));
  CUdeviceptr src, cur, next, branch, main, attn, work, aux, dwa, q[3];
  for (auto p : {&src, &cur, &next, &branch, &main, &attn, &work, &aux, &q[0],
                 &q[1], &q[2]}) {
    ck("alloc", cuMemAlloc(p, A));
    ck("zero", cuMemsetD8(*p, 0, A));
  }
  ck("wa", cuMemAlloc(&dwa, WA));
  ck("input", cuMemcpyHtoD(src, input.data(), A));
  ck("weights", cuMemcpyHtoD(dwa, weights.data(), WA));
  int eight = 8;
  unsigned char rp[0x18]{};
  p64(rp, 0, src);
  p64(rp, 8, cur);
  std::memcpy(rp + 16, &eight, 4);
  std::memcpy(rp + 20, &eight, 4);
  void *ra[] = {rp};
  ck("repack2d1d", cuLaunchKernel(r21, 80, 1, 1, 32, 4, 1, 0, 0, ra, 0));
  ck("sr", cuCtxSynchronize());
  uint64_t dims = 8ull | (8ull << 32), tokens = 64;
  for (int bi = 0; bi < 8; ++bi) {
    for (auto p : {next, branch, main, attn, work, q[0], q[1], q[2]})
      ck("clear", cuMemsetD8(p, 0, A));
    CUdeviceptr base = aux + bi * 0x3000, prev = bi ? base - 0x400 : 0,
                a1 = base + 0xa00, a2 = base + 0xe00, a3 = base + 0x1200,
                a4 = base + 0x1800, a5 = base + 0x1e00, a20 = base + 0x2800,
                a38 = base + 0x2c00, w0 = dwa + ws[bi].e, w1 = dwa + ws[bi].c,
                w2 = dwa + ws[bi].q, w4 = dwa + ws[bi].p;
    unsigned char pe[0x48]{};
    p64(pe, 0, cur);
    p64(pe, 16, branch);
    p64(pe, 24, w0);
    p64(pe, 48, prev);
    p64(pe, 56, base);
    std::memcpy(pe + 64, &tokens, 8);
    void *ae[] = {pe};
    ck("expand", cuLaunchKernel(fe, 32, 1, 1, 32, 4, 1, 0, 0, ae, 0));
    ck("se", cuCtxSynchronize());
    unsigned char pc[0x48]{};
    p64(pc, 0, branch);
    p64(pc, 8, cur);
    p64(pc, 16, main);
    p64(pc, 24, w1);
    p64(pc, 32, a1);
    p64(pc, 40, work);
    p64(pc, 48, base);
    p64(pc, 56, a2);
    std::memcpy(pc + 64, &tokens, 8);
    void *cc[] = {pc};
    CUlaunchConfig lc{};
    CUlaunchAttribute la[2]{};
    cluster(lc, la, 8, 4);
    ck("contract", cuLaunchKernelEx(&lc, fc, cc, nullptr));
    ck("sc", cuCtxSynchronize());
    unsigned char pq[0x50]{};
    p64(pq, 0, main);
    p64(pq, 8, q[0]);
    p64(pq, 16, q[1]);
    p64(pq, 24, q[2]);
    p64(pq, 32, w2);
    p64(pq, 40, a3);
    p64(pq, 48, work);
    p64(pq, 56, a2);
    p64(pq, 64, a4);
    std::memcpy(pq + 72, &dims, 8);
    void *qq[] = {pq};
    cluster(lc, la, 16, 2);
    ck("qkv", cuLaunchKernelEx(&lc, fq, qq, nullptr));
    ck("sq", cuCtxSynchronize());
    unsigned char pa[0x40]{};
    p64(pa, 0, q[0]);
    p64(pa, 8, q[1]);
    p64(pa, 16, q[2]);
    p64(pa, 24, attn);
    p64(pa, 32, work);
    p64(pa, 40, a4);
    p64(pa, 48, a5);
    std::memcpy(pa + 56, &dims, 8);
    void *aa[] = {pa};
    ck("attention", cuLaunchKernel(fa, 32, 1, 1, 32, 4, 1, 0, 0, aa, 0));
    ck("sa", cuCtxSynchronize());
    unsigned char pp[0x48]{};
    p64(pp, 0, attn);
    p64(pp, 8, main);
    p64(pp, 16, next);
    p64(pp, 24, w4);
    p64(pp, 32, a20);
    p64(pp, 40, work);
    p64(pp, 48, a5);
    p64(pp, 56, a38);
    std::memcpy(pp + 64, &dims, 8);
    void *ap[] = {pp};
    cluster(lc, la, 8, 4);
    ck("projection", cuLaunchKernelEx(&lc, fp, ap, nullptr));
    ck("sp", cuCtxSynchronize());
    std::swap(cur, next);
    std::fprintf(stderr, "block%d done\n", bi + 31);
  }
  ck("clearout", cuMemsetD8(next, 0, A));
  unsigned char op[0x18]{};
  p64(op, 0, cur);
  p64(op, 8, next);
  std::memcpy(op + 16, &eight, 4);
  std::memcpy(op + 20, &eight, 4);
  void *oa[] = {op};
  ck("repack1d2d", cuLaunchKernel(r12, 64, 1, 1, 32, 4, 1, 0, 0, oa, 0));
  ck("so", cuCtxSynchronize());
  std::vector<unsigned char> x(A);
  ck("read", cuMemcpyDtoH(x.data(), next, A));
  size_t nz = 0, last = 0, nan = 0;
  for (size_t i = 0; i < A; i++) {
    if (x[i]) {
      ++nz;
      last = i;
    }
    nan += x[i] == 0x7f || x[i] == 0xff;
  }
  std::printf("nonzero=%zu last=%zu nan=%zu\n", nz, last, nan);
  std::ofstream(av[2], std::ios::binary).write((char *)x.data(), A);
  return nan ? 3 : 0;
}
