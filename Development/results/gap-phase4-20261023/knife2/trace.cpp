#include "hip_reference_network.h"
#include "production_options.generated.h"
#include <cstdio>
#include <fstream>
#include <cmath>
#include <cstring>
#include <filesystem>
using namespace hip_reference;
static std::vector<char> read(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("input missing");std::vector<char>b(size_t(f.tellg()));f.seekg(0);if(!f.read(b.data(),b.size()))throw std::runtime_error("input short");return b;}
static void flags(const char*p){std::ifstream f(p);if(!f)throw std::runtime_error("flags missing");std::string s;while(std::getline(f,s)){if(s.empty()||s[0]=='#')continue;auto q=s.find('=');if(q==s.npos)continue;auto k=s.substr(0,q),v=s.substr(q+1);if(!v.empty()&&v.back()=='\r')v.pop_back();if(k.rfind("DLSS5_",0)==0)_putenv_s(k.c_str(),v.c_str());}}
int main(int argc,char**argv){try{
 setvbuf(stdout,nullptr,_IONBF,0);if(argc!=7&&argc!=8)throw std::runtime_error("trace ASSETS MODULES FLAGS PROCESSING_RGBA32F OUTDIR PROFILE(small900|small720|smallfree1088|small1080) [BASELINE_OUTDIR]");flags(argv[3]);const std::string profile=argv[6];U w=0,h=0,nt=0;
 if(profile=="small900"){w=1600;h=1024;nt=448;}else if(profile=="small720"){w=1344;h=768;nt=288;}else if(profile=="smallfree1088"){w=1920;h=1088;nt=640;}else if(profile=="small1080"){w=1920;h=1152;nt=640;}else throw std::runtime_error("unknown profile");
 for(const char*k:{"DLSS5_MULTI_PASS_PREDICT","DLSS5_MULTI_PASS_SKIN_PROTECT","DLSS5_HIP_GRAPH","DLSS5_OVERLAP","DLSS5_VIT_ADAPTIVE","DLSS5_VIT_REUSE_HOTKEY","DLSS5_TEMPORAL_HISTORY_EXPERIMENT","DLSS5_TEMPORAL_MV_UNJITTERED"})_putenv_s(k,"0");
 _putenv_s("DLSS5_MULTI_PASS","0.5");_putenv_s("DLSS5_STYLE","1");_putenv_s("DLSS5_FAST_NUMERIC","1");_putenv_s("DLSS5_HIP_FAST","1");_putenv_s("DLSS5_SKIP_BLOCKS","");
 auto input=read(argv[4]);if(input.size()!=size_t(w)*h*16)throw std::runtime_error("processing input footprint");
 for(size_t i=0;i<input.size();i+=4){float f;std::memcpy(&f,input.data()+i,4);if(!std::isfinite(f))throw std::runtime_error("nonfinite encoded input");}
 auto opt=production_options(argv[1],argv[2],w,h,false);opt.small_geometry=true;opt.integration.allow_small_model=true;opt.integration.allow_vit_hotkey=false;opt.experimental_temporal=false;opt.skip_blocks.clear();opt.profile=false;opt.wall_profile=false;opt.dump_dir.clear();
 if(!opt.wave_owned||!opt.c512_m32||opt.vit_stream!=3)throw std::runtime_error("production profile missing");
 Network net(opt);if(net.ModelMode()!=0||net.EffectiveModelMode()!=0||net.MultiPass()!=1)throw std::runtime_error("actual Small31 required; fallback refused");
 printf("SMALL_TRACE_CASE profile=%s processing=%ux%u expected_tokens=%u small_geometry=1 allow_small_model=1 requested=0.5 effective=0.5 blocks=31 FAST1=1 AE0=1 MP1=1 seed0=1 input_domain=encoded_synthetic observer=0 timing_not_collected=1\n",profile.c_str(),w,h,nt);
 net.SetNoise({});auto&api=net.Runtime();void*in{},*out{};api.Check(api.hipMalloc(&in,input.size()),"allocate input");api.Check(api.hipMalloc(&out,size_t(w)*h*12),"allocate output");api.Check(api.hipMemcpy(in,input.data(),input.size(),1),"upload once");std::filesystem::create_directories(argv[5]);std::vector<float>first;
 for(U frame=0;frame<3;frame++){
  net.SmallTraceBegin(frame,nt);net.Enqueue(in,nullptr,out,0);net.Synchronize();net.SmallTraceEnd();std::vector<float>v(size_t(w)*h*3);api.Check(api.hipMemcpy(v.data(),out,v.size()*4,2),"read RGB");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("nonfinite trace RGB");
  if(frame==0)first=v;else if(std::memcmp(first.data(),v.data(),v.size()*4))throw std::runtime_error("three static frames must repeat bitwise");
  std::ofstream f(std::filesystem::path(argv[5])/("frame-"+std::to_string(frame)+".rgb32f"),std::ios::binary);if(!f.write(reinterpret_cast<char*>(v.data()),v.size()*4))throw std::runtime_error("RGB write failed");f.close();
  if(argc==8){const auto ref=read((std::filesystem::path(argv[7])/("frame-"+std::to_string(frame)+".rgb32f")).string().c_str());if(ref.size()!=v.size()*4)throw std::runtime_error("paired baseline footprint mismatch");size_t changed=0;for(size_t j=0;j<v.size();j++)changed+=std::memcmp(ref.data()+j*4,&v[j],4)!=0;printf("SMALL_TRACE_PAIR frame=%u raw_bitdiff=%zu\n",frame,changed);if(changed)throw std::runtime_error("paired baseline trace RGB differs");}
 }
 printf("SMALL_TRACE_DONE profile=%s frames=3 finite=1 repeat_bitdiff=0 timing_not_collected=1\n",profile.c_str());api.hipFree(in);api.hipFree(out);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"SMALL_TRACE_FAIL %s\n",e.what());return 1;}}
