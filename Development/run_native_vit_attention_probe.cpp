#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(CUresult r){if(r){const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"CUDA %d %s\n",r,s?s:"");std::exit(1);}}
static std::vector<unsigned char> read(const char*p,size_t size){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)std::exit(2);auto n=f.tellg();if(n<=0||size_t(n)>4*1024*1024)std::exit(2);std::vector<unsigned char>b(size_t(n),0);f.seekg(0);if(!f.read((char*)b.data(),n))std::exit(2);for(size_t i=size;i<b.size();i++)if(b[i])std::exit(2);b.resize(size,0);return b;}
int main(int argc,char**argv){
 if(argc!=8){std::fprintf(stderr,"usage: %s q k v output width height gridX\n",argv[0]);return 2;}
 unsigned width=std::strtoul(argv[5],nullptr,0),height=std::strtoul(argv[6],nullptr,0),gx=std::strtoul(argv[7],nullptr,0);if(!width||!height||((width>16||height>16)&&!(width==32&&height==20))||!gx||gx>256)return 2;
 unsigned gy=(width*height+255)/256;
 size_t capacity=4*1024*1024,padded=(width*height+127)/128*128;
 std::vector<unsigned char>input[]={read(argv[1],padded*1024),read(argv[2],padded*1024),read(argv[3],padded*2048)};
 ck(cuInit(0));CUdevice d;ck(cuDeviceGet(&d,0));CUcontext context;ck(cuDevicePrimaryCtxRetain(&context,d));ck(cuCtxSetCurrent(context));CUmodule module;const char*cubin=std::getenv("DLSS5_VIT_CUBIN");ck(cuModuleLoad(&module,cubin?cubin:"/tmp/dlssnr-cubins/dlssnr-05.cubin"));CUfunction f;ck(cuModuleGetFunction(&f,module,"cc_vit_1d_attention_fp8"));
 CUdeviceptr q[3],out,work,aux;for(unsigned i=0;i<3;i++){ck(cuMemAlloc(&q[i],input[i].size()));ck(cuMemcpyHtoD(q[i],input[i].data(),input[i].size()));}for(auto*p:{&out,&work,&aux}){ck(cuMemAlloc(p,capacity));ck(cuMemsetD8(*p,0,capacity));}
 CUdeviceptr fields[]={q[0],q[1],q[2],out,work,aux+0x1800,aux+0x1e00};alignas(8) unsigned char p[0x40]{};std::memcpy(p,fields,56);std::memcpy(p+56,&height,4);std::memcpy(p+60,&width,4);void*args[]={p};
 std::fprintf(stderr,"attention launch extent=%ux%u grid=%u,%u\n",width,height,gx,gy);ck(cuLaunchKernel(f,gx,gy,1,32,4,1,0,nullptr,args,nullptr));ck(cuCtxSynchronize());std::vector<unsigned char>host(capacity);ck(cuMemcpyDtoH(host.data(),out,capacity));size_t nonzero=0,last=0,nan=0;for(size_t i=0;i<host.size();i++){if(host[i]){nonzero++;last=i;}nan+=(host[i]&127)==127;}std::printf("nonzero=%zu last=%zu nan=%zu\n",nonzero,last,nan);std::ofstream stream(argv[4],std::ios::binary);if(!stream.write((char*)host.data(),host.size()))return 2;for(auto ptr:{q[0],q[1],q[2],out,work,aux})ck(cuMemFree(ptr));return nan?3:0;
}
