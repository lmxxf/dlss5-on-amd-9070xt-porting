#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda.h>
#include <fstream>
#include <string>
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
static void cluster(CUlaunchConfig &c, CUlaunchAttribute &a, unsigned gx,
                    unsigned z) {
  a = {};
  a.id = CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;
  a.value.clusterDim.x = 1;
  a.value.clusterDim.y = 1;
  a.value.clusterDim.z = z;
  c = {};
  c.gridDimX = gx;
  c.gridDimY = 1;
  c.gridDimZ = z;
  c.blockDimX = 32;
  c.blockDimY = 4;
  c.blockDimZ = 1;
  c.attrs = &a;
  c.numAttrs = 1;
}
int main(int ac, char **av) {
  if (ac != 8) {
    std::fprintf(stderr, "usage: %s input output off0 off1 off2 off4 arena\n",
                 av[0]);
    return 2;
  }
  constexpr size_t A = 2097152, WA = 147719680;
  auto in = rd(av[1], A), weights = rd(av[7], WA);
  uint64_t off0 = strtoull(av[3], 0, 0), off1 = strtoull(av[4], 0, 0),
           off2 = strtoull(av[5], 0, 0), off4 = strtoull(av[6], 0, 0);
  ck("init", cuInit(0));
  CUdevice d;
  ck("dev", cuDeviceGet(&d, 0));
  CUcontext ctx;
  ck("ctx", cuDevicePrimaryCtxRetain(&ctx, d));
  ck("set", cuCtxSetCurrent(ctx));
  CUstream stream_standard, stream_chained;
  int least_priority = 0, greatest_priority = 0;
  ck("priority_range",
     cuCtxGetStreamPriorityRange(&least_priority, &greatest_priority));
  ck("stream_standard", cuStreamCreateWithPriority(
                            &stream_standard, CU_STREAM_NON_BLOCKING,
                            least_priority));
  ck("stream_chained", cuStreamCreateWithPriority(
                           &stream_chained, CU_STREAM_NON_BLOCKING,
                           greatest_priority));
  CUmodule m;
  ck("module", cuModuleLoad(&m, "/tmp/dlssnr-cubins/dlssnr-05.cubin"));
  CUfunction fe, fc, fc_chained, fq, fa, fp, fp_wait;
  ck("fe", cuModuleGetFunction(&fe, m, "cc_vit_1d_ffn_expand_fp8"));
  ck("fc", cuModuleGetFunction(&fc, m, "cc_vit_1d_ffn_contract_fp8"));
  ck("fc_chained", cuModuleGetFunction(
                         &fc_chained, m,
                         "cc_vit_1d_ffn_contract_chained_fp8"));
  ck("fq", cuModuleGetFunction(&fq, m, "cc_vit_1d_qkv_fp8"));
  ck("fa", cuModuleGetFunction(&fa, m, "cc_vit_1d_attention_fp8"));
  ck("fp", cuModuleGetFunction(&fp, m, "cc_vit_1d_projection_fp8"));
  ck("fp_wait", cuModuleGetFunction(&fp_wait, m,
                                     "cc_vit_1d_projection_wait_fp8"));
  CUdeviceptr din, branch, main, attn, out, work, aux, dwa, q[3];
  for (auto p :
       {&din, &branch, &main, &attn, &out, &work, &aux, &q[0], &q[1], &q[2]}) {
    ck("alloc", cuMemAlloc(p, A));
    ck("zero", cuMemsetD8(*p, 0, A));
  }
  ck("wa", cuMemAlloc(&dwa, WA));
  ck("input", cuMemcpyHtoD(din, in.data(), A));
  ck("weights", cuMemcpyHtoD(dwa, weights.data(), WA));
  CUdeviceptr w0 = dwa + off0, w1 = dwa + off1, w2 = dwa + off2,
              w4 = dwa + off4;
  uint64_t dims = 8ull | (8ull << 32), tokens = 64;
  unsigned char pe[0x48]{};
  p64(pe, 0, din);
  p64(pe, 16, branch);
  p64(pe, 24, w0);
  p64(pe, 56, aux);
  std::memcpy(pe + 64, &tokens, 8);
  void *ae[] = {pe};
  ck("expand", cuLaunchKernel(fe, 32, 1, 1, 32, 4, 1, 0, 0, ae, 0));
  ck("se", cuCtxSynchronize());
  CUdeviceptr a1 = aux + 0xa00, a2 = aux + 0xe00, a3 = aux + 0x1200,
              a4 = aux + 0x1800, a5 = aux + 0x1e00, a20 = aux + 0x2800,
              a38 = aux + 0x2c00;
  unsigned char pc[0x48]{};
  p64(pc, 0, branch);
  p64(pc, 8, din);
  p64(pc, 16, main);
  p64(pc, 24, w1);
  p64(pc, 32, a1);
  p64(pc, 40, work);
  p64(pc, 48, aux);
  p64(pc, 56, a2);
  std::memcpy(pc + 64, &tokens, 8);
  void *cc[] = {pc};
  CUlaunchAttribute ca{};
  CUlaunchConfig cfg{};
  cluster(cfg, ca, 8, 4);
  CUlaunchConfig chained_cfg = cfg;
  cfg.hStream = stream_standard;
  chained_cfg.hStream = stream_chained;
  ck("contract_chained",
     cuLaunchKernelEx(&chained_cfg, fc_chained, cc, nullptr));
  ck("contract", cuLaunchKernelEx(&cfg, fc, cc, nullptr));
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
  CUlaunchAttribute qa{};
  cluster(cfg, qa, 16, 2);
  ck("qkv", cuLaunchKernelEx(&cfg, fq, qq, nullptr));
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
  p64(pp, 16, out);
  p64(pp, 24, w4);
  p64(pp, 32, a20);
  p64(pp, 40, work);
  p64(pp, 48, a5);
  p64(pp, 56, a38);
  std::memcpy(pp + 64, &dims, 8);
  void *ap[] = {pp};
  CUlaunchAttribute pca{};
  cluster(cfg, pca, 8, 4);
  cfg.hStream = stream_standard;
  chained_cfg = cfg;
  chained_cfg.hStream = stream_chained;
  ck("projection", cuLaunchKernelEx(&cfg, fp, ap, nullptr));
  ck("projection_wait", cuLaunchKernelEx(&chained_cfg, fp_wait, ap, nullptr));
  ck("sp", cuCtxSynchronize());
  std::vector<unsigned char> x(A);
  ck("read", cuMemcpyDtoH(x.data(), out, A));
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
