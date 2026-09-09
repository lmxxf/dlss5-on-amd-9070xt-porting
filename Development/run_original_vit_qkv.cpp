#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(const char*n,CUresult r){if(!r)return;const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"%s=%d %s\n",n,r,s?s:"?");std::exit(1);}
static std::vector<unsigned char>rd(const char*p,size_t n){std::vector<unsigned char>x(n);std::ifstream f(p,std::ios::binary);if(!f||!f.read((char*)x.data(),n))std::exit(2);return x;}
int main(int ac,char**av){
  if(ac!=11){std::fprintf(stderr,"usage: %s arena weight-offset input work aux q k v work-out aux-out\n",av[0]);return 2;}
  constexpr size_t A=2097152,WA=147719680;auto w=rd(av[1],WA),in=rd(av[3],A),hostWork=rd(av[4],A),hostAux=rd(av[5],A);
  ck("init",cuInit(0));CUdevice d;ck("dev",cuDeviceGet(&d,0));CUcontext c;ck("ctx",cuDevicePrimaryCtxRetain(&c,d));ck("set",cuCtxSetCurrent(c));CUmodule m;ck("module",cuModuleLoad(&m,"/tmp/dlssnr-cubins/dlssnr-05.cubin"));CUfunction f;ck("function",cuModuleGetFunction(&f,m,"cc_vit_1d_qkv_fp8"));
  CUdeviceptr di,q[3],work,aux,dw;for(auto*p:{&di,&q[0],&q[1],&q[2],&work,&aux}){ck("alloc",cuMemAlloc(p,A));ck("zero",cuMemsetD8(*p,0,A));}ck("wa",cuMemAlloc(&dw,WA));ck("weights",cuMemcpyHtoD(dw,w.data(),WA));ck("input",cuMemcpyHtoD(di,in.data(),A));
  ck("work_upload",cuMemcpyHtoD(work,hostWork.data(),A));ck("aux_upload",cuMemcpyHtoD(aux,hostAux.data(),A));CUdeviceptr a2=aux+0xe00,a3=aux+0x1200,a4=aux+0x1800,wv=dw+std::strtoull(av[2],nullptr,0);uint64_t dims=8ull|(8ull<<32);unsigned char p[0x50]{};std::memcpy(p,&di,8);std::memcpy(p+8,&q[0],8);std::memcpy(p+16,&q[1],8);std::memcpy(p+24,&q[2],8);std::memcpy(p+32,&wv,8);std::memcpy(p+40,&a3,8);std::memcpy(p+48,&work,8);std::memcpy(p+56,&a2,8);std::memcpy(p+64,&a4,8);std::memcpy(p+72,&dims,8);void*args[]={p};
  CUlaunchAttribute at{};at.id=CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;at.value.clusterDim={1,1,2};CUlaunchConfig cfg{};cfg.gridDimX=16;cfg.gridDimY=1;cfg.gridDimZ=2;cfg.blockDimX=32;cfg.blockDimY=4;cfg.blockDimZ=1;cfg.attrs=&at;cfg.numAttrs=1;ck("launch",cuLaunchKernelEx(&cfg,f,args,nullptr));ck("sync",cuCtxSynchronize());
  std::vector<unsigned char>x(A);for(int i=0;i<3;i++){ck("read",cuMemcpyDtoH(x.data(),q[i],A));std::ofstream(av[6+i],std::ios::binary).write((char*)x.data(),x.size());}ck("read_work",cuMemcpyDtoH(x.data(),work,A));std::ofstream(av[9],std::ios::binary).write((char*)x.data(),x.size());ck("read_aux",cuMemcpyDtoH(x.data(),aux,A));std::ofstream(av[10],std::ios::binary).write((char*)x.data(),x.size());return 0;
}
