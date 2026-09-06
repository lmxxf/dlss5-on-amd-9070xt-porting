// Experimental block39 ABI probe. Compile/preflight do not launch a GPU.
// Grid and spatial layout remain candidates until original-output validation.
#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static void ck(CUresult r) {
  if (r != CUDA_SUCCESS) {
    const char* s=nullptr; cuGetErrorString(r,&s);
    std::fprintf(stderr,"CUDA %d: %s\n",r,s?s:""); std::exit(1);
  }
}
static unsigned number(const char* s) {
  char* end=nullptr; unsigned long n=std::strtoul(s,&end,10);
  if (!*s || *end || n<4 || n>32 || n%4) std::exit(2);
  return unsigned(n);
}
static std::vector<unsigned char> read(const char* path,size_t max,bool exact=false) {
  std::ifstream f(path,std::ios::binary|std::ios::ate);
  if (!f) { std::fprintf(stderr,"cannot read %s\n",path); std::exit(2); }
  auto n=f.tellg();
  if (n<=0 || size_t(n)>max || (exact && size_t(n)!=max)) std::exit(2);
  std::vector<unsigned char> v(static_cast<size_t>(n));
  f.seekg(0); if (!f.read((char*)v.data(),n)) std::exit(2);
  return v;
}
static void write(const std::string& path,const std::vector<unsigned char>& v) {
  // Refuse an existing result rather than silently replacing an old oracle.
  if (std::ifstream(path).good()) { std::fprintf(stderr,"exists: %s\n",path.c_str()); std::exit(2); }
  std::ofstream f(path,std::ios::binary);
  if (!f.write((const char*)v.data(),v.size())) std::exit(2);
}
int main(int argc,char** argv) {
  if (argc!=8 && argc!=9) {
    std::fprintf(stderr,"usage: %s cubin main.fp8 skip.fp8 weights output-prefix width height [--run]\n",argv[0]); return 2;
  }
  const bool run=argc==9 && !std::strcmp(argv[8],"--run");
  if (argc==9 && !run) return 2;
  const unsigned w=number(argv[6]),h=number(argv[7]);
  constexpr size_t capacity=2*1024*1024;
  auto main=read(argv[2],capacity),skip=read(argv[3],capacity),weights=read(argv[4],525312,true);
  for (const char* suffix:{".output.fp8",".partial.bin",".counters.i32"})
    if (std::ifstream(std::string(argv[5])+suffix).good()) return 2;
  const unsigned gx=2*((w+3)/4),gy=(h+3)/4;
  std::printf("candidate grid=%u,%u,4 block=32,2,1 main=%zu skip=%zu weights=%zu counters=-1\n",gx,gy,main.size(),skip.size(),weights.size());
  if (!run) { std::puts("preflight only; no CUDA initialization or numerical acceptance"); return 0; }
  std::fflush(stdout);
  ck(cuInit(0)); CUdevice d; ck(cuDeviceGet(&d,0)); CUcontext context;
  ck(cuDevicePrimaryCtxRetain(&context,d)); ck(cuCtxSetCurrent(context));
  CUmodule module; ck(cuModuleLoad(&module,argv[1])); CUfunction fn;
  ck(cuModuleGetFunction(&fn,module,"cc_dec_input_upsample_1024_512_fp8"));
  CUdeviceptr input,encoder,output,counters,partial,weight;
  for (auto ptr:{&input,&encoder,&output,&counters,&partial}) {
    ck(cuMemAlloc(ptr,capacity)); ck(cuMemsetD8(*ptr,0,capacity));
  }
  ck(cuMemsetD32(counters,0xffffffffu,capacity/4));
  ck(cuMemAlloc(&weight,weights.size()));
  ck(cuMemcpyHtoD(input,main.data(),main.size()));
  ck(cuMemcpyHtoD(encoder,skip.data(),skip.size()));
  ck(cuMemcpyHtoD(weight,weights.data(),weights.size()));
  // +0x10 is final output; +0x20 is Z-order counters; +0x30 partial sums.
  CUdeviceptr fields[]={input,encoder,output,0,counters,0,partial,weight};
  alignas(8) unsigned char params[0x50]{}; std::memcpy(params,fields,sizeof(fields));
  unsigned dims[]={h,w,h*2,w*2}; std::memcpy(params+0x40,dims,sizeof(dims));
  void* args[]={params};
  // Z partitions communicate through global counters. Keep all four in a cluster.
  CUlaunchAttribute attr{}; attr.id=CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;
  attr.value.clusterDim={1,1,4}; CUlaunchConfig cfg{};
  cfg.gridDimX=gx; cfg.gridDimY=gy; cfg.gridDimZ=4;
  cfg.blockDimX=32; cfg.blockDimY=2; cfg.blockDimZ=1;
  cfg.attrs=&attr; cfg.numAttrs=1;
  ck(cuLaunchKernelEx(&cfg,fn,args,nullptr)); ck(cuCtxSynchronize());
  std::vector<unsigned char> host(capacity);
  for (auto item:std::vector<std::pair<CUdeviceptr,std::string>>{
      {output,".output.fp8"},{partial,".partial.bin"},{counters,".counters.i32"}}) {
    ck(cuMemcpyDtoH(host.data(),item.first,capacity)); write(std::string(argv[5])+item.second,host);
  }
  for (auto ptr:{input,encoder,output,counters,partial,weight}) ck(cuMemFree(ptr));
  ck(cuModuleUnload(module)); ck(cuDevicePrimaryCtxRelease(d));
  std::puts("executed; outputs require independent validation, not a pass");
}
