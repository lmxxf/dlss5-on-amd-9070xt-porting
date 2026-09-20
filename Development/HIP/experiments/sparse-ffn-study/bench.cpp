#include "hip_api.h"
#include <vector>
#include <fstream>
#include <chrono>
#include <cstdio>
using namespace hip_probe;
int main(){try{
 Api api;api.Check(api.hipInit(0),"init");api.Check(api.hipSetDevice(0),"device");Handle stream{},mod{},old{},sparse{},pack{},dense{};api.Check(api.hipStreamCreate(&stream),"stream");
 api.Check(api.LoadModule(&mod,"gfx1201.hsaco"),"module");api.Check(api.LoadModule(&old,"../vit-residual-adaptive-r3-modules/deep_fast-packed.hsaco"),"old");
 api.Check(api.hipModuleGetFunction(&sparse,mod,"sparse_expand_frag"),"sparse");api.Check(api.hipModuleGetFunction(&pack,old,"vit_pack_input"),"pack");api.Check(api.hipModuleGetFunction(&dense,old,"vit_expand_blocked_fp8_frag_bytein"),"dense");
 auto alloc=[&](size_t n){void*p{};api.Check(api.hipMalloc(&p,n),"alloc");return p;};
 auto upload=[&](const char*name){std::ifstream f(name,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(name);std::vector<char>b(size_t(f.tellg()));f.seekg(0);f.read(b.data(),b.size());void*p=alloc(b.size());api.Check(api.hipMemcpy(p,b.data(),b.size(),1),"upload");return p;};
 unsigned M=400,K=1024,N=4096,count=M*K;void*x=upload("../int4-gpu-data/input.f32"),*xp=alloc(count),*w=upload("fragments.fp8"),*ix=upload("fragment-indices.u32"),*wd=upload("../int4-gpu-data/fp8-frag.bin"),*out=alloc(M*N),*pre=alloc(M*N*4),*nil=nullptr;
 auto launch=[&](Handle f,unsigned grid,unsigned threads,void**args){api.Check(api.hipModuleLaunchKernel(f,grid,1,1,threads,1,1,0,stream,args,nullptr),"launch");};
 auto run=[&](bool candidate,bool dump){void*pa[]={&x,&xp,&count,&nil};launch(pack,M,256,pa);if(candidate){void*p=dump?pre:nil;void*a[]={&xp,&w,&ix,&out,&p,&M};launch(sparse,M*4,32,a);}else{void*a[]={&xp,&wd,&out,&M,&K,&N,&nil};launch(dense,M*4,32,a);}};
 run(true,true);api.Check(api.hipStreamSynchronize(stream),"sync");std::vector<char>b(M*N*4);api.Check(api.hipMemcpy(b.data(),pre,b.size(),2),"read");std::ofstream("pre.f32",std::ios::binary).write(b.data(),b.size());
 std::vector<char>expected[2];for(int q=0;q<2;q++){run(q,false);api.Check(api.hipStreamSynchronize(stream),"reference");expected[q].resize(M*N);api.Check(api.hipMemcpy(expected[q].data(),out,M*N,2),"output reference");std::ofstream(q?"sparse.fp8":"dense.fp8",std::ios::binary).write(expected[q].data(),M*N);}
 FILE*f=fopen("timing.csv","wb");fprintf(f,"round,slot,sparse,ms\n");
 for(int r=0;r<2;r++)for(int slot=0;slot<4;slot++){bool s=slot==1||slot==2;for(int i=0;i<2000;i++)run(s,false);api.Check(api.hipStreamSynchronize(stream),"warm");auto t=std::chrono::steady_clock::now();for(int i=0;i<10000;i++)run(s,false);api.Check(api.hipStreamSynchronize(stream),"timing");double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count()/10000;fprintf(f,"%d,%d,%d,%.9f\n",r,slot,s,ms);printf("%d %d %d %.6f\n",r,slot,s,ms);fflush(stdout);std::vector<char>got(M*N);api.Check(api.hipMemcpy(got.data(),out,M*N,2),"verify batch");if(got!=expected[s])throw std::runtime_error("batch output changed");}fclose(f);return 0;
}catch(const std::exception&e){fprintf(stderr,"FAILED %s\n",e.what());return 1;}}
