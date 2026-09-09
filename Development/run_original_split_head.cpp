// Diagnostic caller for the candidate 512 -> 1024 encoder head.
#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void ck(CUresult r){if(r){const char*s=nullptr;cuGetErrorString(r,&s);std::fprintf(stderr,"CUDA %d %s\n",r,s?s:"");std::exit(1);}}
static std::vector<unsigned char> read(const char*p,size_t limit,bool exact){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)std::exit(2);auto n=f.tellg();if(n<=0||size_t(n)>limit||(exact&&size_t(n)!=limit))std::exit(2);std::vector<unsigned char>b(limit);f.seekg(0);if(!f.read((char*)b.data(),n))std::exit(2);return b;}
int main(int argc,char**argv){
 if(argc!=10){std::fprintf(stderr,"usage: %s input output weights width height gridX gridY gridZ blockY\n",argv[0]);return 2;}
 int w=std::atoi(argv[4]),h=std::atoi(argv[5]),gx=std::atoi(argv[6]),gy=std::atoi(argv[7]),gz=std::atoi(argv[8]),by=std::atoi(argv[9]);
 if(w<4||h<4||w>64||h>64||w%4||h%4||gx<1||gx>128||gy<1||gy>16||gz!=1||(by!=4&&by!=8))return 2;
 const size_t capacity=4*1024*1024;auto input=read(argv[1],capacity,false),weights=read(argv[3],524304,true);
 ck(cuInit(0));CUdevice device;ck(cuDeviceGet(&device,0));CUcontext context;ck(cuDevicePrimaryCtxRetain(&context,device));ck(cuCtxSetCurrent(context));CUmodule module;ck(cuModuleLoad(&module,"/tmp/dlssnr-cubins/dlssnr-04.cubin"));CUfunction f;ck(cuModuleGetFunction(&f,module,"cc_split_swin_16h_final_head_512_fp8"));
 CUdeviceptr src,dst,weight;ck(cuMemAlloc(&src,capacity));ck(cuMemAlloc(&dst,capacity));ck(cuMemAlloc(&weight,weights.size()));ck(cuMemcpyHtoD(src,input.data(),capacity));ck(cuMemsetD8(dst,0,capacity));ck(cuMemcpyHtoD(weight,weights.data(),weights.size()));
 alignas(8) unsigned char p[0x28]{};std::memcpy(p,&src,8);std::memcpy(p+8,&dst,8);std::memcpy(p+16,&weight,8);std::memcpy(p+32,&h,4);std::memcpy(p+36,&w,4);void*args[]={p};
 ck(cuLaunchKernel(f,gx,gy,gz,32,by,1,0,nullptr,args,nullptr));ck(cuCtxSynchronize());std::vector<unsigned char>out(capacity);ck(cuMemcpyDtoH(out.data(),dst,capacity));
 size_t nonzero=0,last=0,nan=0;for(size_t i=0;i<out.size();i++){if(out[i]){nonzero++;last=i;}nan+=(out[i]&127)==127;}
 std::printf("nonzero=%zu last=%zu nan=%zu\n",nonzero,last,nan);std::ofstream stream(argv[2],std::ios::binary);if(!stream.write((char*)out.data(),out.size()))return 2;ck(cuMemFree(src));ck(cuMemFree(dst));ck(cuMemFree(weight));return nan?3:0;
}
