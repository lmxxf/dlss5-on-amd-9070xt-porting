#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(CUresult r){if(r){const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"CUDA %d %s\n",r,s?s:"");std::exit(1);}}
static std::vector<unsigned char> read(const char*p,size_t size,bool exact){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)std::exit(2);auto n=f.tellg();if(n<=0||size_t(n)>size||(exact&&size_t(n)!=size))std::exit(2);std::vector<unsigned char>b(size);f.seekg(0);if(!f.read((char*)b.data(),n))std::exit(2);return b;}
int main(int argc,char**argv){
 if(argc!=6){std::fprintf(stderr,"usage: %s input output weights tokens gridX\n",argv[0]);return 2;}
 uint64_t tokens=std::strtoull(argv[4],nullptr,0);unsigned gx=std::strtoul(argv[5],nullptr,0);if(!tokens||tokens>256||!gx||gx>128)return 2;
 const size_t capacity=4*1024*1024;auto input=read(argv[1],size_t((tokens+31)/32*32)*1024,false),weights=read(argv[3],4194320,true);
 ck(cuInit(0));CUdevice d;ck(cuDeviceGet(&d,0));CUcontext context;ck(cuDevicePrimaryCtxRetain(&context,d));ck(cuCtxSetCurrent(context));CUmodule module;ck(cuModuleLoad(&module,"/tmp/dlssnr-cubins/dlssnr-05.cubin"));CUfunction f;ck(cuModuleGetFunction(&f,module,"cc_vit_1d_ffn_expand_fp8"));
 CUdeviceptr src,dst,weight,aux;ck(cuMemAlloc(&src,input.size()));ck(cuMemAlloc(&dst,capacity));ck(cuMemAlloc(&aux,capacity));ck(cuMemAlloc(&weight,weights.size()));ck(cuMemcpyHtoD(src,input.data(),input.size()));ck(cuMemsetD8(dst,0,capacity));ck(cuMemsetD8(aux,0,capacity));ck(cuMemcpyHtoD(weight,weights.data(),weights.size()));
 alignas(8) unsigned char p[0x48]{};std::memcpy(p,&src,8);std::memcpy(p+16,&dst,8);std::memcpy(p+24,&weight,8);std::memcpy(p+56,&aux,8);std::memcpy(p+64,&tokens,8);void*args[]={p};
 ck(cuLaunchKernel(f,gx,1,1,32,4,1,0,nullptr,args,nullptr));ck(cuCtxSynchronize());std::vector<unsigned char>out(capacity);ck(cuMemcpyDtoH(out.data(),dst,capacity));size_t nonzero=0,last=0,nan=0;for(size_t i=0;i<out.size();i++){if(out[i]){nonzero++;last=i;}nan+=(out[i]&127)==127;}
 std::printf("nonzero=%zu last=%zu nan=%zu\n",nonzero,last,nan);std::ofstream stream(argv[2],std::ios::binary);if(!stream.write((char*)out.data(),out.size()))return 2;for(auto ptr:{src,dst,weight,aux})ck(cuMemFree(ptr));return nan?3:0;
}
