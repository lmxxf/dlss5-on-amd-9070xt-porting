#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

static void ck(const char *n, CUresult r) { if (r) { const char *s=nullptr; cuGetErrorString(r,&s); std::fprintf(stderr,"%s=%d %s\n",n,r,s?s:"?"); std::exit(1); } }
static std::vector<unsigned char> rd(const char *p, size_t n) { std::vector<unsigned char>x(n); std::ifstream f(p,std::ios::binary); if(!f)std::exit(2); f.read((char*)x.data(),n); return x; }
static void put64(unsigned char *p,size_t o,CUdeviceptr v){std::memcpy(p+o,&v,8);} static void put32(unsigned char*p,size_t o,int v){std::memcpy(p+o,&v,4);}

int main(int argc,char **argv){
    if(argc!=10){std::fprintf(stderr,"usage: %s input output w0 w1 w2 w3 width height shifted\n",argv[0]);return 2;}
    constexpr size_t A=4*1024*1024; int width=std::atoi(argv[7]),height=std::atoi(argv[8]);
    auto input=rd(argv[1],A),w0=rd(argv[3],524288),w1=rd(argv[4],263168),w2=rd(argv[5],917568),w3=rd(argv[6],263168),host=std::vector<unsigned char>(A);
    ck("init",cuInit(0));CUdevice d;ck("dev",cuDeviceGet(&d,0));CUcontext c;ck("ctx",cuDevicePrimaryCtxRetain(&c,d));ck("set",cuCtxSetCurrent(c));CUmodule m;ck("module",cuModuleLoad(&m,"/tmp/dlssnr-cubins/dlssnr-04.cubin"));
    const bool shifted=std::atoi(argv[9])!=0; const char*qkv="cc_split_swin_16h_qkv_512_fp8";
    CUfunction f0,f1,f2,f3;ck("f0",cuModuleGetFunction(&f0,m,"cc_split_swin_16h_ffwd_512_fp8"));ck("f1",cuModuleGetFunction(&f1,m,"cc_split_swin_16h_ffwd_proj_512_fp8"));ck("f2",cuModuleGetFunction(&f2,m,qkv));ck("f3",cuModuleGetFunction(&f3,m,"cc_split_swin_16h_proj_512_fp8"));
    CUdeviceptr din,branch,ffn,attn,out,dw0,dw1,dw2,dw3;for(auto p:{&din,&branch,&ffn,&attn,&out}){ck("alloc",cuMemAlloc(p,A));ck("zero",cuMemsetD8(*p,0,A));}ck("aw0",cuMemAlloc(&dw0,w0.size()));ck("aw1",cuMemAlloc(&dw1,w1.size()));ck("aw2",cuMemAlloc(&dw2,w2.size()));ck("aw3",cuMemAlloc(&dw3,w3.size()));ck("in",cuMemcpyHtoD(din,input.data(),A));ck("w0",cuMemcpyHtoD(dw0,w0.data(),w0.size()));ck("w1",cuMemcpyHtoD(dw1,w1.data(),w1.size()));ck("w2",cuMemcpyHtoD(dw2,w2.data(),w2.size()));ck("w3",cuMemcpyHtoD(dw3,w3.data(),w3.size()));
    int gx=(width+7)/8,gy=(height+7)/8; alignas(8) unsigned char p0[0x38]{};put64(p0,0,din);put64(p0,8,branch);put64(p0,16,dw0);put32(p0,24,width);put32(p0,28,height);void*a0[]={p0};ck("l0",cuLaunchKernel(f0,gx,gy,2,32,4,1,0,0,a0,0));ck("s0",cuCtxSynchronize());
    alignas(8) unsigned char p1[0x48]{};put64(p1,0,din);put64(p1,8,branch);put64(p1,16,ffn);put64(p1,24,dw1);put32(p1,32,width);put32(p1,36,height);void*a1[]={p1};ck("l1",cuLaunchKernel(f1,gx,gy,1,32,4,1,0,0,a1,0));ck("s1",cuCtxSynchronize());
    alignas(8) unsigned char p2[0x38]{};put64(p2,0,ffn);put64(p2,8,attn);put64(p2,16,dw2);put32(p2,24,width);put32(p2,28,height);put32(p2,32,shifted?-4:0);put32(p2,36,shifted?-4:0);void*a2[]={p2};ck("l2",cuLaunchKernel(f2,gx,gy,4,32,4,1,0,0,a2,0));ck("s2",cuCtxSynchronize());
    ck("read_ffn",cuMemcpyDtoH(host.data(),ffn,A));std::ofstream(std::string(argv[2])+".ffn",std::ios::binary).write((char*)host.data(),A);ck("read_attn",cuMemcpyDtoH(host.data(),attn,A));std::ofstream(std::string(argv[2])+".attn",std::ios::binary).write((char*)host.data(),A);
    alignas(8) unsigned char p3[0x48]{};put64(p3,0,attn);put64(p3,8,ffn);put64(p3,16,out);put64(p3,24,dw3);put32(p3,32,width);put32(p3,36,height);void*a3[]={p3};ck("l3",cuLaunchKernel(f3,gx,gy,1,32,4,1,0,0,a3,0));ck("sync",cuCtxSynchronize());ck("read",cuMemcpyDtoH(host.data(),out,A));
    size_t nz=0,last=0,nan=0;for(size_t i=0;i<A;i++){if(host[i]){nz++;last=i;}nan+=host[i]==0x7f||host[i]==0xff;}std::set<unsigned char>u(host.begin(),host.begin()+last+1);std::printf("nonzero=%zu last=%zu unique=%zu nan=%zu\n",nz,last,u.size(),nan);std::ofstream(argv[2],std::ios::binary).write((char*)host.data(),A);return nan?3:0;
}
