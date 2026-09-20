// Direct GPU edge checks: equality includes padding and signed-zero bits.
#include "hip_api.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
using namespace hip_probe;using U=uint32_t;
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("module argument");Api api;api.Check(api.hipInit(0),"init");api.Check(api.hipSetDevice(0),"device");Handle module{};api.Check(api.LoadModule(&module,argv[1]),"module");std::vector<void*>allocated;
 auto alloc=[&](size_t bytes){void*p{};api.Check(api.hipMalloc(&p,bytes),"allocate");allocated.push_back(p);return p;};
 auto launch=[&](const char*name,U groups,void**args){Handle f{};api.Check(api.hipModuleGetFunction(&f,module,name),name);api.Check(api.hipModuleLaunchKernel(f,groups,1,1,32,1,1,0,nullptr,args,nullptr),name);};
 for(U n: {400u,640u}){U rw=n==400?25:30,rh=n==400?15:18,stride=n==400?25:32,count=n*1024;
  std::vector<float>anchor(count,1.f),current(count,1.f),y(count,2.f),out(count),gain(1024,.0625f);void*x=alloc(count*4),*a=alloc(count*4),*z=alloc(count*4),*g=alloc(4096),*result=alloc(count*4),*stats=alloc(n*16),*state=alloc(32),*image=alloc(16),*sig=alloc(48),*image_anchor=alloc(48);
  api.Check(api.hipMemcpy(a,anchor.data(),count*4,1),"anchor");api.Check(api.hipMemcpy(z,y.data(),count*4,1),"output anchor");api.Check(api.hipMemcpy(g,gain.data(),4096,1),"gain");float zeros[4]{};api.Check(api.hipMemcpy(image,zeros,16,1),"image delta");float signature[12];for(U i=0;i<12;i++)signature[i]=float(i);api.Check(api.hipMemcpy(sig,signature,48,1),"signature");
  for(U test=0;test<7;test++){current=anchor;U words[8]={0,3,1,0,0,0,0,0},period=4,mode=1,tiles=4,cols=2;float global=.22f,local=1.f,image_limit=.35f;U expected=8;
   if(test==1){U padded=0;for(U t=0;t<n;t++){U r=(t&~15u)|((t&1u)<<3)|((t&14u)>>1);if(r/stride>=rh||r%stride>=rw){padded=t;break;}}current[padded*1024]=1.125f;expected=3;}
   if(test==2){anchor[0]=0.f;current=anchor;U negzero=0x80000000u;memcpy(current.data(),&negzero,4);api.Check(api.hipMemcpy(a,anchor.data(),count*4,1),"zero anchor");expected=3;}
   if(test==3){mode=2;expected=2;}
   if(test==4){period=1;expected=3;}
   if(test==5){words[1]=100;expected=8;}
   if(test==6){words[1]=0;U nan=0x7fc00000;memcpy(current.data()+1024,&nan,4);expected=4;}
   api.Check(api.hipMemcpy(x,current.data(),count*4,1),"input");api.Check(api.hipMemcpy(state,words,32,1),"state");void*sa[]={&x,&a,&state,&stats,&n,&rw,&rh,&stride};launch("reuse_token_stats",n,sa);
   void*da[]={&stats,&state,&n,&period,&global,&local,&mode,&image,&tiles,&cols,&image_limit};launch("reuse_decide",1,da);api.Check(api.hipDeviceSynchronize(),"decide sync");api.Check(api.hipMemcpy(words,state,32,2),"state read");if(words[6]!=expected)throw std::runtime_error("decision n="+std::to_string(n)+" case="+std::to_string(test)+" got="+std::to_string(words[6]));
   if(expected==8&&(!words[0]||words[1]>period))throw std::runtime_error("exact reuse/age saturation");
   if(test==0){U image_count=12;void*fa[]={&x,&z,&a,&z,&g,&state,&result,&count,&sig,&image_anchor,&image_count};launch("reuse_finish",count/256,fa);api.Check(api.hipDeviceSynchronize(),"finish sync");api.Check(api.hipMemcpy(out.data(),result,count*4,2),"result");if(memcmp(out.data(),y.data(),count*4))throw std::runtime_error("identical input result differs");}
   printf("PASS n=%u case=%u reason=%u\n",n,test,words[6]);
  }
  U words[8]{};api.Check(api.hipMemcpy(state,words,32,1),"full state");U image_count=12;void*fa[]={&x,&z,&a,&z,&g,&state,&result,&count,&sig,&image_anchor,&image_count};launch("reuse_finish",count/256,fa);api.Check(api.hipDeviceSynchronize(),"full sync");float got[12];api.Check(api.hipMemcpy(got,image_anchor,48,2),"image anchor");if(memcmp(got,signature,48))throw std::runtime_error("fused image commit");printf("PASS n=%u fused image commit\n",n);
 }
 for(void*p:allocated)api.hipFree(p);api.hipModuleUnload(module);return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
