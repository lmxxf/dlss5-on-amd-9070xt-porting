#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(CUresult r){if(r){const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"CUDA %d %s\n",r,s?s:"");std::exit(1);}}
static std::vector<unsigned char> read(const char*p,size_t size,bool exact){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)std::exit(2);auto n=f.tellg();if(n<=0||size_t(n)>8*1024*1024||(exact&&size_t(n)!=size))std::exit(2);std::vector<unsigned char>b(size_t(n),0);f.seekg(0);if(!f.read((char*)b.data(),n))std::exit(2);for(size_t i=size;i<b.size();i++)if(b[i])std::exit(2);b.resize(size,0);return b;}
static void write(const std::string&p,const std::vector<unsigned char>&v){std::ofstream f(p,std::ios::binary);if(!f.write((const char*)v.data(),v.size()))std::exit(2);}
int main(int argc,char**argv){
 if(argc!=7&&argc!=8){std::fprintf(stderr,"usage: %s branch residual weights output tokens gridX [projection]\n",argv[0]);return 2;}
 const bool projection=argc==8&&!std::strcmp(argv[7],"projection");if(argc==8&&!projection)return 2;
 uint64_t tokens=std::strtoull(argv[5],nullptr,0);unsigned gx=std::strtoul(argv[6],nullptr,0);if(!tokens||tokens>640||!gx||gx>128)return 2;
 if(tokens!=16&&tokens!=64&&tokens!=128&&tokens!=256&&tokens!=640)return 2;
 if(projection&&tokens!=64&&tokens!=256&&tokens!=640)return 2;
 size_t padded=projection?size_t((tokens+127)/128*128):size_t((tokens+31)/32*32),capacity=4*1024*1024;
 auto branch=read(argv[1],padded*(projection?1024:4096),false),residual=read(argv[2],padded*1024,false),weights=read(argv[3],projection?1050624:4196352,true);
 ck(cuInit(0));CUdevice d;ck(cuDeviceGet(&d,0));CUcontext context;ck(cuDevicePrimaryCtxRetain(&context,d));ck(cuCtxSetCurrent(context));CUmodule module;const char*cubin=std::getenv("DLSS5_VIT_CUBIN");ck(cuModuleLoad(&module,cubin?cubin:"/tmp/dlssnr-cubins/dlssnr-05.cubin"));CUfunction f;ck(cuModuleGetFunction(&f,module,projection?"cc_vit_1d_projection_fp8":"cc_vit_1d_ffn_contract_fp8"));
 CUdeviceptr a,b,out,w,work,aux;ck(cuMemAlloc(&a,branch.size()));ck(cuMemAlloc(&b,residual.size()));ck(cuMemAlloc(&w,weights.size()));for(auto*p:{&out,&work,&aux}){ck(cuMemAlloc(p,capacity));ck(cuMemsetD8(*p,0,capacity));}
 ck(cuMemcpyHtoD(a,branch.data(),branch.size()));ck(cuMemcpyHtoD(b,residual.data(),residual.size()));ck(cuMemcpyHtoD(w,weights.data(),weights.size()));
 // Z0 publishes 0; Zk waits for >= k-1. Zero initialization lets Z1
 // overtake Z0 and lose its completion counter. Each counter starts at -1.
 ck(cuMemsetD32(aux+0xa00,0xffffffffu,0x400/4));
 CUdeviceptr fields[]={a,b,out,w,aux+0xa00,work,aux,aux+0xe00};alignas(8) unsigned char p[0x48]{};std::memcpy(p,fields,64);std::memcpy(p+64,&tokens,8);void*args[]={p};
 const unsigned width=tokens==640?32:tokens==16?4:tokens==64?8:16,height=unsigned(tokens)/width;const unsigned dims[]={height,width};std::memcpy(p+64,dims,8);
 CUlaunchAttribute attr{};attr.id=CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;attr.value.clusterDim={1,1,4};CUlaunchConfig config{};config.gridDimX=gx;config.gridDimY=1;config.gridDimZ=4;config.blockDimX=32;config.blockDimY=4;config.blockDimZ=1;config.attrs=&attr;config.numAttrs=1;
 std::fprintf(stderr,"%s launch counters=-1 tokens=%llu gridX=%u\n",projection?"projection":"contract",(unsigned long long)tokens,gx);
 ck(cuLaunchKernelEx(&config,f,args,nullptr));ck(cuCtxSynchronize());std::vector<unsigned char>host(capacity);ck(cuMemcpyDtoH(host.data(),out,capacity));size_t nonzero=0,last=0,nan=0;for(size_t i=0;i<host.size();i++){if(host[i]){nonzero++;last=i;}nan+=(host[i]&127)==127;}std::printf("nonzero=%zu last=%zu nan=%zu\n",nonzero,last,nan);write(argv[4],host);ck(cuMemcpyDtoH(host.data(),work,capacity));write(std::string(argv[4])+".work",host);for(auto ptr:{a,b,out,w,work,aux})ck(cuMemFree(ptr));return nan?3:0;
}
