#include "hip_api.h"
#include <vector>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <string>
#include <cstdio>
#ifndef INT4_TILE
#define INT4_TILE 128
#endif
#define STR2(x) #x
#define STR(x) STR2(x)
static_assert(INT4_TILE==16 || INT4_TILE==128);
using namespace hip_probe;using U=unsigned;
std::vector<char> read(const std::string&p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(p);std::vector<char>b(size_t(f.tellg()));f.seekg(0);f.read(b.data(),b.size());return b;}
int main(int argc,char**argv){try{if(argc!=5)throw std::runtime_error("DATA R3_MODULE INT4_MODULE OUT");Api api;api.Check(api.hipInit(0),"init");api.Check(api.hipSetDevice(0),"device");auto prop=api.Properties(0);if(std::string(prop.gcnArchName).find("gfx1201")!=0)throw std::runtime_error("probe expects9070XT/gfx1201");Handle old{},fresh{},stream{};api.Check(api.LoadModule(&old,argv[2]),"R3");api.Check(api.LoadModule(&fresh,argv[3]),"INT4");api.Check(api.hipStreamCreate(&stream),"stream");std::vector<void*>owned;
 auto alloc=[&](size_t n){void*p{};api.Check(api.hipMalloc(&p,n),"malloc");owned.push_back(p);return p;};
 auto upload=[&](const char*name){auto b=read(std::string(argv[1])+"/"+name);void*p=alloc(b.size());api.Check(api.hipMemcpy(p,b.data(),b.size(),1),"upload");return p;};
 auto fn=[&](Handle m,const char*n){Handle f{};api.Check(api.hipModuleGetFunction(&f,m,n),n);return f;};
 auto launch=[&](Handle f,U gx,U gy,U threads,void**args){api.Check(api.hipModuleLaunchKernel(f,gx,gy,1,threads,1,1,0,stream,args,nullptr),"launch");};
 U M=400,K=1024,N=4096,count=M*K;void*x=upload("input.f32"),*wf8=upload("fp8-frag.bin"),*smooth=upload("smooth-inv.f32"),*ones=upload("ones.f32"),*x8=alloc(count),*x4=alloc(count/2),*scale=alloc(M*16*4),*out=alloc(size_t(M)*N),*pre=alloc(size_t(M)*N*4),*no_pre=nullptr,*gate=nullptr;
 Handle pack=fn(old,"vit_pack_input"),fp8=fn(old,"vit_expand_blocked_fp8_frag_bytein"),row=fn(fresh,"q4_row_wave"),group=fn(fresh,"q4_group"),gemmrow=fn(fresh,"int4_expand_row"),gemmgroup=fn(fresh,"int4_expand_group"),gemmgroup32=fn(fresh,"int4_expand_group_n" STR(INT4_TILE));
 float balanced_clip;auto cb=read(std::string(argv[1])+"/balanced-clip.f32");memcpy(&balanced_clip,cb.data(),4);
 void*wf[5]={upload("plain-frag.bin"),upload("rotrow-frag.bin"),upload("rotgroup-frag.bin"),upload("balanced-frag.bin")};void*ws[5]={upload("plain-scale.f32"),upload("rotrow-scale.f32"),upload("rotgroup-scale.f32"),upload("balanced-scale.f32")};
 wf[4]=wf[2];ws[4]=ws[2];
 auto run=[&](int method,bool dump,int part=0){
  if(!method){void*pa[]={&x,&x8,&count,&gate};if(part!=2)launch(pack,M,1,256,pa);void*ga[]={&x8,&wf8,&out,&M,&K,&N,&gate};if(part!=1)launch(fp8,M*4,1,32,ga);}
  else{int j=method-1;U rot=(j==1||j==2||j==4)?1:0,grouped=(j==2||j==4);float clip=j==3?balanced_clip:1.f;void*s=j?smooth:ones;void*qa[]={&x,&s,&x4,&scale,&M,&rot,&clip};if(part!=2)launch(grouped?group:row,M,grouped?16:1,32,qa);void*p=dump?pre:no_pre;void*ga[]={&x4,&wf[j],&scale,&ws[j],&out,&p,&M,&grouped};if(part!=1)launch(j==4?gemmgroup32:grouped?gemmgroup:gemmrow,M*(j==4?256/INT4_TILE:4),1,32,ga);}
 };
 for(int method=0;method<6;method++){run(method,true);api.Check(api.hipStreamSynchronize(stream),"check sync");std::vector<char>b(size_t(M)*N);api.Check(api.hipMemcpy(b.data(),out,b.size(),2),"read output");std::ofstream(std::string(argv[4])+"/method"+std::to_string(method)+".fp8",std::ios::binary).write(b.data(),b.size());if(method){b.resize(count/2);api.Check(api.hipMemcpy(b.data(),x4,b.size(),2),"read quantized input");std::ofstream(std::string(argv[4])+"/method"+std::to_string(method)+"-input.i4",std::ios::binary).write(b.data(),b.size());b.resize(M*((method==3||method==5)?16:1)*4);api.Check(api.hipMemcpy(b.data(),scale,b.size(),2),"read scale");std::ofstream(std::string(argv[4])+"/method"+std::to_string(method)+"-scale.f32",std::ios::binary).write(b.data(),b.size());b.resize(size_t(M)*N*4);api.Check(api.hipMemcpy(b.data(),pre,b.size(),2),"read pre");std::ofstream(std::string(argv[4])+"/method"+std::to_string(method)+"-pre.f32",std::ios::binary).write(b.data(),b.size());}}
 auto check_output=[&](int method){auto expected=read(std::string(argv[4])+"/method"+std::to_string(method)+".fp8");std::vector<char>got(expected.size());api.Check(api.hipMemcpy(got.data(),out,got.size(),2),"batch result");if(got!=expected)throw std::runtime_error("batch changed output");};
 FILE*csv=fopen((std::string(argv[4])+"/timing.csv").c_str(),"wb");fprintf(csv,"candidate,slot,method,mean_ms\n");
 for(int candidate: {3,5,5,3}){for(int slot=0;slot<4;slot++){int method=(slot==0||slot==3)?0:candidate;for(int i=0;i<2000;i++)run(method,false);api.Check(api.hipStreamSynchronize(stream),"warm");auto start=std::chrono::steady_clock::now();for(int i=0;i<10000;i++)run(method,false);api.Check(api.hipStreamSynchronize(stream),"batch");double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10000;fprintf(csv,"%d,%d,%d,%.9f\n",candidate,slot,method,ms);printf("candidate=%d slot=%d method=%d ms=%.6f\n",candidate,slot,method,ms);fflush(stdout);check_output(method);}}
 fclose(csv);for(void*p:owned)api.hipFree(p);api.hipStreamDestroy(stream);api.hipModuleUnload(fresh);api.hipModuleUnload(old);return 0;
}catch(const std::exception&e){fprintf(stderr,"FAILED: %s\n",e.what());return 1;}}
