#include <cuda.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(const char*n,CUresult r){if(!r)return;const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"%s=%d %s\n",n,r,s?s:"?");std::exit(1);}
static std::vector<unsigned char>rd(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)std::exit(2);size_t n=f.tellg();f.seekg(0);std::vector<unsigned char>x(n);f.read((char*)x.data(),n);return x;}
int main(int ac,char**av){
 if(ac!=12){std::fprintf(stderr,"usage: %s arena off work aux input-off q-off k-off v-off dataset output metadata\n",av[0]);return 2;}constexpr size_t A=2097152,WA=147719680;auto weights=rd(av[1]),hw=rd(av[3]),ha=rd(av[4]),io=rd(av[5]),qo=rd(av[6]),ko=rd(av[7]),vo=rd(av[8]),data=rd(av[9]);if(weights.size()!=WA||hw.size()!=A||ha.size()!=A||io.size()!=4096||qo.size()!=4096||ko.size()!=4096||vo.size()!=4096||data.size()%1024)return 2;size_t samples=data.size()/1024;auto*inOff=(uint32_t*)io.data();std::array<const uint32_t*,3>outOff={(uint32_t*)qo.data(),(uint32_t*)ko.data(),(uint32_t*)vo.data()};
 ck("init",cuInit(0));CUdevice d;ck("dev",cuDeviceGet(&d,0));CUcontext c;ck("ctx",cuDevicePrimaryCtxRetain(&c,d));ck("set",cuCtxSetCurrent(c));CUmodule m;ck("module",cuModuleLoad(&m,"/tmp/dlssnr-cubins/dlssnr-05.cubin"));CUfunction f;ck("function",cuModuleGetFunction(&f,m,"cc_vit_1d_qkv_fp8"));CUdeviceptr input,out[3],work,aux,dw;for(auto*p:{&input,&out[0],&out[1],&out[2],&work,&aux}){ck("alloc",cuMemAlloc(p,A));ck("zero",cuMemsetD8(*p,0,A));}ck("wa",cuMemAlloc(&dw,WA));ck("weights",cuMemcpyHtoD(dw,weights.data(),WA));ck("work",cuMemcpyHtoD(work,hw.data(),A));ck("aux",cuMemcpyHtoD(aux,ha.data(),A));CUdeviceptr a2=aux+0xe00,a3=aux+0x1200,a4=aux+0x1800,wv=dw+std::strtoull(av[2],nullptr,0);uint64_t dims=8ull|(8ull<<32);unsigned char p[0x50]{};std::memcpy(p,&input,8);std::memcpy(p+8,&out[0],8);std::memcpy(p+16,&out[1],8);std::memcpy(p+24,&out[2],8);std::memcpy(p+32,&wv,8);std::memcpy(p+40,&a3,8);std::memcpy(p+48,&work,8);std::memcpy(p+56,&a2,8);std::memcpy(p+64,&a4,8);std::memcpy(p+72,&dims,8);void*args[]={p};CUlaunchAttribute at{};at.id=CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;at.value.clusterDim={1,1,2};CUlaunchConfig cfg{};cfg.gridDimX=16;cfg.gridDimY=1;cfg.gridDimZ=2;cfg.blockDimX=32;cfg.blockDimY=4;cfg.blockDimZ=1;cfg.attrs=&at;cfg.numAttrs=1;
 std::vector<unsigned char>hi(A),ho(A);std::ofstream dst(av[10],std::ios::binary);for(size_t s=0;s<samples;s++){std::fill(hi.begin(),hi.end(),0);for(int i=0;i<1024;i++)hi[inOff[i]]=data[s*1024+i];ck("input",cuMemcpyHtoD(input,hi.data(),A));for(auto x:out)ck("clear",cuMemsetD8(x,0,A));ck("launch",cuLaunchKernelEx(&cfg,f,args,nullptr));ck("sync",cuCtxSynchronize());for(int g=0;g<3;g++){ck("read",cuMemcpyDtoH(ho.data(),out[g],A));for(int j=0;j<1024;j++)dst.put((char)ho[outOff[g][j]]);}}
 std::ofstream meta(av[11]);meta<<"samples="<<samples<<" inputs=1024 groups=3 outputs=1024\n";std::printf("samples=%zu output_bytes=%zu\n",samples,samples*3*1024);return 0;
}
