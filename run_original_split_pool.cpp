#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(CUresult r){if(r){const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"CUDA %d %s\n",r,s?s:"");std::exit(1);}}
static std::vector<unsigned char> read(const char*path,size_t size,bool exact){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)std::exit(2);auto n=f.tellg();if(n<0||(exact&&size_t(n)!=size)||size_t(n)>size)std::exit(2);std::vector<unsigned char>b(size);f.seekg(0);if(!f.read((char*)b.data(),n))std::exit(2);return b;}
static void write(const char*path,const std::vector<unsigned char>&b){std::ofstream f(path,std::ios::binary);if(!f.write((const char*)b.data(),b.size()))std::exit(2);}
int main(int argc,char**argv){
 if(argc!=8&&argc!=10){std::fprintf(stderr,"usage: %s attn ffn weights main pool width height [poolWidth poolHeight]\n",argv[0]);return 2;}
 int w=std::atoi(argv[6]),h=std::atoi(argv[7]);if(w<4||h<4||w>64||h>64||w%4||h%4)return 2;
 int pw=argc==10?std::atoi(argv[8]):w/2,ph=argc==10?std::atoi(argv[9]):h/2;
 if(pw<w/2||ph<h/2||pw>32||ph>32)return 2;
 const size_t capacity=4*1024*1024;auto att=read(argv[1],capacity,false),ffn=read(argv[2],capacity,false),weights=read(argv[3],263168,true);
 ck(cuInit(0));CUdevice device;ck(cuDeviceGet(&device,0));CUcontext context;ck(cuDevicePrimaryCtxRetain(&context,device));ck(cuCtxSetCurrent(context));CUmodule module;ck(cuModuleLoad(&module,"/tmp/dlssnr-cubins/dlssnr-04.cubin"));CUfunction function;ck(cuModuleGetFunction(&function,module,"cc_split_swin_16h_proj_pool_512_fp8"));
 CUdeviceptr buffers[5];for(int i=0;i<5;i++){ck(cuMemAlloc(&buffers[i],capacity));ck(cuMemsetD8(buffers[i],0,capacity));}
 ck(cuMemcpyHtoD(buffers[0],att.data(),capacity));ck(cuMemcpyHtoD(buffers[1],ffn.data(),capacity));ck(cuMemcpyHtoD(buffers[4],weights.data(),weights.size()));
 // Metadata: one 0x50-byte parameter. SASS loads views 0/8, stores 16/24,
 // weights at 32, full extent at 64/68 and pooled extent at 72/76.
 alignas(8) unsigned char p[0x50]{};for(int i=0;i<5;i++)std::memcpy(p+8*i,&buffers[i],8);
 int dims[]={h,w,ph,pw};std::memcpy(p+64,dims,16);void*args[]={p};
 ck(cuLaunchKernel(function,2*((w+7)/8),(h+7)/8,1,32,4,1,0,nullptr,args,nullptr));ck(cuCtxSynchronize());
 std::vector<unsigned char>out(capacity);ck(cuMemcpyDtoH(out.data(),buffers[2],capacity));write(argv[4],out);ck(cuMemcpyDtoH(out.data(),buffers[3],capacity));write(argv[5],out);
 for(auto b:buffers)ck(cuMemFree(b));return 0;
}
