#include "hip_api.h"
#include <vector>
#include <random>
#include <cstring>
#include <cstdio>
// Random trials; operands include large/small magnitudes and cancellation so the accumulator rounding is exercised.
int main(int argc,char**argv){try{
 hip_probe::Api a(7);a.Check(a.hipInit(0),"init");a.Check(a.hipSetDevice(0),"dev");
 hip_probe::Handle m{},f8{},f16{};a.Check(a.hipModuleLoad(&m,argv[1]),"load");
 a.Check(a.hipModuleGetFunction(&f8,m,"probe_fp8"),"fn8");a.Check(a.hipModuleGetFunction(&f16,m,"probe_f16"),"fn16");
 const unsigned K=256;std::mt19937 rng(1234);void*dX,*dY,*dC,*dO;
 a.Check(a.hipMalloc(&dX,16*K*2),"m");a.Check(a.hipMalloc(&dY,16*K*2),"m");a.Check(a.hipMalloc(&dC,1024),"m");a.Check(a.hipMalloc(&dO,2048),"m");
 unsigned long long diff8=0,diff16=0,total=0;
 for(int trial=0;trial<2000;trial++){
  std::vector<unsigned char> xb(16*K),yb(16*K);for(auto&v:xb){v=rng()&255;if((v&0x7f)==0x7f)v^=1;}for(auto&v:yb){v=rng()&255;if((v&0x7f)==0x7f)v^=1;}// avoid e4m3 NaN codes
  std::vector<unsigned short> xh(16*K),yh(16*K);std::uniform_real_distribution<float> u(-1,1);
  for(unsigned i=0;i<16*K;i++){_Float16 p=(_Float16)(u(rng)*(trial%3==0?1000.f:trial%3==1?1.f:1e-3f)),q=(_Float16)(u(rng)*(trial%2?64.f:0.01f));memcpy(&xh[i],&p,2);memcpy(&yh[i],&q,2);}
  std::vector<float> c(256);for(auto&v:c)v=u(rng)*(trial%4==0?0.f:trial%4==1?1.f:trial%4==2?1e4f:1e-4f);
  a.Check(a.hipMemcpy(dC,c.data(),1024,1),"c");unsigned k=K;std::vector<unsigned> o(512);
  a.Check(a.hipMemcpy(dX,xb.data(),16*K,1),"x");a.Check(a.hipMemcpy(dY,yb.data(),16*K,1),"y");
  {void*args[]={&dX,&dY,&dC,&dO,&k};a.Check(a.hipModuleLaunchKernel(f8,1,1,1,32,1,1,0,{},args,nullptr),"l8");a.Check(a.hipDeviceSynchronize(),"s");a.Check(a.hipMemcpy(o.data(),dO,2048,2),"b");
   for(int i=0;i<256;i++)diff8+=o[i]!=o[256+i];}
  a.Check(a.hipMemcpy(dX,xh.data(),16*K*2,1),"x");a.Check(a.hipMemcpy(dY,yh.data(),16*K*2,1),"y");
  {void*args[]={&dX,&dY,&dC,&dO,&k};a.Check(a.hipModuleLaunchKernel(f16,1,1,1,32,1,1,0,{},args,nullptr),"l16");a.Check(a.hipDeviceSynchronize(),"s");a.Check(a.hipMemcpy(o.data(),dO,2048,2),"b");
   for(int i=0;i<256;i++)diff16+=o[i]!=o[256+i];}
  total+=256;}
 printf("fp8 wmma(Y,X,C^T) vs wmma(X,Y,C)^T: %llu / %llu values differ\nf16: %llu / %llu values differ\n",diff8,total,diff16,total);
 return diff8||diff16;}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 2;}}
