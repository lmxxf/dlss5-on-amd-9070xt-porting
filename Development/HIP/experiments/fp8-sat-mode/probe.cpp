#include "hip_api.h"
#include <vector>
#include <cmath>
#include <limits>
int main(int argc,char**argv){try{
 hip_probe::Api a(7);a.Check(a.hipInit(0),"init");a.Check(a.hipSetDevice(0),"dev");
 hip_probe::Handle m{},f{};a.Check(a.hipModuleLoad(&m,argv[1]),"load");a.Check(a.hipModuleGetFunction(&f,m,"probe"),"fn");
 float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
 std::vector<float> v={0.f,-0.f,1.f,447.f,448.f,455.f,463.9f,464.f,470.f,480.f,500.f,1000.f,65504.f,70000.f,1e30f,inf,-inf,nan,-nan,-470.f,-1000.f,1e-6f};
 unsigned n=v.size();void*din,*dout;a.Check(a.hipMalloc(&din,n*4),"m");a.Check(a.hipMalloc(&dout,n*16),"m");a.Check(a.hipMemcpy(din,v.data(),n*4,1),"cp");
 for(unsigned bit:{0u,1u}){void*args[]={&din,&dout,&n,&bit};a.Check(a.hipModuleLaunchKernel(f,1,1,1,32,1,1,0,{},args,nullptr),"launch");a.Check(a.hipDeviceSynchronize(),"sync");
  std::vector<unsigned> o(n*4);a.Check(a.hipMemcpy(o.data(),dout,n*16,2),"back");
  printf("MODE.FP16_OVFL=%u\n%12s %8s %8s %8s %8s\n",bit,"x","clamped","raw","f16","med3");
  for(unsigned i=0;i<n;i++)printf("%12g     0x%02x     0x%02x   0x%04x     0x%02x\n",v[i],o[i*4],o[i*4+1],o[i*4+2],o[i*4+3]);}
 return 0;}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 2;}}
