#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>
static void ck(CUresult r){if(r){const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"CUDA %d %s\n",r,s?s:"");std::exit(1);}}
static std::vector<unsigned char> read(const char*p,size_t size,bool exact){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)std::exit(2);auto n=f.tellg();if(n<=0||size_t(n)>8*1024*1024||(exact&&size_t(n)!=size))std::exit(2);std::vector<unsigned char>b(size_t(n),0);f.seekg(0);if(!f.read((char*)b.data(),n))std::exit(2);for(size_t i=size;i<b.size();i++)if(b[i])std::exit(2);b.resize(size,0);return b;}
int main(int argc,char**argv){
 if(argc!=7){std::fprintf(stderr,"usage: %s input weights output-prefix width height gridX\n",argv[0]);return 2;}
 unsigned width=std::strtoul(argv[4],nullptr,0),height=std::strtoul(argv[5],nullptr,0),gx=std::strtoul(argv[6],nullptr,0);if(!width||!height||((width>16||height>16)&&!(width==32&&height==20))||!gx||gx>128)return 2;
 size_t capacity=4*1024*1024,padded=(width*height+127)/128*128;
 auto input=read(argv[1],padded*1024,false),weights=read(argv[2],3145856,true);
 ck(cuInit(0));CUdevice d;ck(cuDeviceGet(&d,0));CUcontext context;ck(cuDevicePrimaryCtxRetain(&context,d));ck(cuCtxSetCurrent(context));CUmodule module;const char*cubin=std::getenv("DLSS5_VIT_CUBIN");ck(cuModuleLoad(&module,cubin?cubin:"/tmp/dlssnr-cubins/dlssnr-05.cubin"));CUfunction f;ck(cuModuleGetFunction(&f,module,"cc_vit_1d_qkv_fp8"));
 CUdeviceptr src,w,q[3],work,aux;ck(cuMemAlloc(&src,input.size()));ck(cuMemAlloc(&w,weights.size()));for(auto*p:{&q[0],&q[1],&q[2],&work,&aux}){ck(cuMemAlloc(p,capacity));ck(cuMemsetD8(*p,0,capacity));}
 ck(cuMemcpyHtoD(src,input.data(),input.size()));ck(cuMemcpyHtoD(w,weights.data(),weights.size()));ck(cuMemsetD32(aux+0x1200,0xffffffffu,0x400/4));
 CUdeviceptr fields[]={src,q[0],q[1],q[2],w,aux+0x1200,work,aux+0xe00,aux+0x1800};alignas(8) unsigned char p[0x50]{};std::memcpy(p,fields,72);std::memcpy(p+72,&height,4);std::memcpy(p+76,&width,4);void*args[]={p};
 CUlaunchAttribute attr{};attr.id=CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;attr.value.clusterDim={1,1,2};CUlaunchConfig config{};config.gridDimX=gx;config.gridDimY=1;config.gridDimZ=2;config.blockDimX=32;config.blockDimY=4;config.blockDimZ=1;config.attrs=&attr;config.numAttrs=1;
 const bool v_layout=std::getenv("DLSS5_VIT_QKV_V_LAYOUT_SCAN")!=nullptr;
 if(std::getenv("DLSS5_VIT_QKV_UNIT_SCAN")||v_layout){
  if(width!=4||height!=4||gx!=16)return 2;
  std::fill(input.begin(),input.end(),0x38);if(std::getenv("DLSS5_VIT_QKV_SCAN_VALID_ONLY"))std::fill(input.begin()+width*height*1024,input.end(),0);ck(cuMemcpyHtoD(src,input.data(),input.size()));std::fill(weights.begin(),weights.end(),0);
  const bool prefix=v_layout||std::getenv("DLSS5_VIT_QKV_PREFIX_SCALE")!=nullptr;
  const float one=1;for(size_t i=0;i<128;i+=4)std::memcpy(weights.data()+(prefix?0:3145728)+i,&one,4);
  unsigned probes=v_layout?21:23,parts=v_layout?4:1;std::vector<unsigned char>scan(probes*parts*32768);
  for(unsigned probe=0;probe<probes;probe++){
   size_t index=probe?size_t(1)<<(probe-1):0;
   size_t offset=v_layout?128+(index/1024)*3072+2048+index%1024:(prefix?128:0)+index;weights[offset]=0x38;ck(cuMemcpyHtoD(w,weights.data(),weights.size()));weights[offset]=0;
   for(unsigned part=0;part<parts;part++){
   if(v_layout){std::fill(input.begin(),input.end(),0);for(size_t i=0;i<width*height*1024;i++)input[i]=0x20+((i>>(4*part))&15);ck(cuMemcpyHtoD(src,input.data(),input.size()));}
   for(auto ptr:{q[0],q[1],q[2],work,aux})ck(cuMemsetD8(ptr,0,capacity));ck(cuMemsetD32(aux+0x1200,0xffffffffu,0x400/4));
   ck(cuLaunchKernelEx(&config,f,args,nullptr));ck(cuCtxSynchronize());ck(cuMemcpyDtoH(scan.data()+(probe*parts+part)*32768,q[2],32768));
   }
  }
  std::ofstream out(std::string(argv[3])+(v_layout?"-v-layout.fp8":"-unit-scan.fp8"),std::ios::binary);if(!out.write((char*)scan.data(),scan.size()))return 2;
  for(auto ptr:{src,w,q[0],q[1],q[2],work,aux})ck(cuMemFree(ptr));return 0;
 }
 std::fprintf(stderr,"qkv launch counters=-1 extent=%ux%u gridX=%u\n",width,height,gx);ck(cuLaunchKernelEx(&config,f,args,nullptr));ck(cuCtxSynchronize());
 std::vector<unsigned char>host(capacity);for(unsigned part=0;part<3;part++){ck(cuMemcpyDtoH(host.data(),q[part],capacity));size_t nonzero=0,last=0,nan=0;for(size_t i=0;i<host.size();i++){if(host[i]){nonzero++;last=i;}nan+=(host[i]&127)==127;}std::printf("part=%u nonzero=%zu last=%zu nan=%zu\n",part,nonzero,last,nan);std::ofstream out(std::string(argv[3])+"-"+std::to_string(part)+".fp8",std::ios::binary);if(!out.write((char*)host.data(),host.size()))return 2;}
 ck(cuMemcpyDtoH(host.data(),work,capacity));std::ofstream out(std::string(argv[3])+".work",std::ios::binary);if(!out.write((char*)host.data(),host.size()))return 2;for(auto ptr:{src,w,q[0],q[1],q[2],work,aux})ck(cuMemFree(ptr));return 0;
}
