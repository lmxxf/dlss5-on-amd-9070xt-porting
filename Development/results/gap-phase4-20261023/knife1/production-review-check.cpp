#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <vector>
#include <iostream>
#define HIP_VIT_ATTN_KSPLIT_HOST 0
using U=unsigned;
struct Opt{bool graph=false,vit_byte_stream=false,vit_half_stream=false,vit_qkv_fused=true,vit_qkv_frag=true,vit_qkv_fp8=true,vit_attn_fused=true;std::set<U>skip_blocks;std::string dup_prefix,dump_dir;};
struct S{Opt opt;U W=1920,H=1088,multi_pass=1,vit_stream_active=3;bool small_active=false,fast_numeric=true,adaptive_active=false,observer=false,vit_contract_byte_edge=true;std::set<std::string>missing;std::vector<std::string>probes;bool HasFn(const std::string&m,const std::string&n){probes.push_back(m+":"+n);return !missing.count(n);}
 bool vfrag_requested=false,vfrag_available=false,vfrag_ae_off=false;unsigned long long vfrag_producer_calls=0,vfrag_consumer_calls=0;
 void InitVitVFragment(){const char*v=std::getenv("NR_VIT_VFRAG");if(!v||!*v)v=std::getenv("DLSS5_HIP_VIT_VFRAG");vfrag_requested=!v||!*v||std::strcmp(v,"1")==0;const char*ae=std::getenv("DLSS5_VIT_ADAPTIVE");vfrag_ae_off=ae&&std::strcmp(ae,"0")==0;if(vfrag_requested)vfrag_available=HasFn("vit_stream","vit_stream_qkv_frag_bin_w5f8_vfrag")&&HasFn("deep_fast","vit_attention_fused_400_bytein_bout_vfrag")&&HasFn("deep_fast","vit_attention_fused_640_bytein_bout_vfrag");}
 bool VitVFragmentEligible(U n){
  const bool shape=(W==1600&&H==960)||(W==1920&&(H==1088||H==1152));
  if(!vfrag_requested||!vfrag_available||!vfrag_ae_off||!shape||small_active||!fast_numeric||multi_pass!=1||adaptive_active||opt.graph||!opt.skip_blocks.empty()||!opt.dup_prefix.empty()||observer||!opt.dump_dir.empty()||opt.vit_byte_stream||opt.vit_half_stream||vit_stream_active!=3||!vit_contract_byte_edge||!opt.vit_qkv_fused||!opt.vit_qkv_frag||!opt.vit_qkv_fp8||!opt.vit_attn_fused||(n!=400&&n!=640))return false;
  if(HIP_VIT_ATTN_KSPLIT_HOST&&HasFn("deep_fast",std::string(n==400?"vit_attention_fused_400_bytein_bout":"vit_attention_fused_640_bytein_bout")+"_ks2"))return false;
  return true;
 }
};
void env(const char*n,const char*v){if(v)setenv(n,v,1);else unsetenv(n);}
void reset(){env("NR_VIT_VFRAG",nullptr);env("DLSS5_HIP_VIT_VFRAG",nullptr);env("DLSS5_VIT_ADAPTIVE","0");}
int main(){unsigned cases=0;
struct E{const char*nr;const char*prod;bool expected;};
for(auto e:{E{nullptr,nullptr,true},E{nullptr,"0",false},E{nullptr,"1",true},E{"0","1",false},E{"1","0",true},E{"","0",false},E{"","",true},E{"bad","1",false},E{nullptr,"bad",false}}){reset();env("NR_VIT_VFRAG",e.nr);env("DLSS5_HIP_VIT_VFRAG",e.prod);S s;s.InitVitVFragment();assert(s.vfrag_requested==e.expected);assert(s.VitVFragmentEligible(640)==e.expected);assert(s.probes.size()==(e.expected?3:0));cases++;}
for(auto n:{"vit_stream_qkv_frag_bin_w5f8_vfrag","vit_attention_fused_400_bytein_bout_vfrag","vit_attention_fused_640_bytein_bout_vfrag"}){reset();S s;s.missing.insert(n);s.InitVitVFragment();assert(!s.vfrag_available&&!s.VitVFragmentEligible(640));s.missing.clear();assert(!s.VitVFragmentEligible(640));cases++;}
{reset();S s;s.small_active=true;s.InitVitVFragment();assert(s.probes.size()==3&&!s.VitVFragmentEligible(640));s.small_active=false;assert(s.VitVFragmentEligible(640));s.small_active=true;assert(!s.VitVFragmentEligible(640));cases++;}
{reset();S s;s.multi_pass=2;s.InitVitVFragment();assert(s.probes.size()==3&&!s.VitVFragmentEligible(640));s.multi_pass=1;assert(s.VitVFragmentEligible(640));s.multi_pass=3;assert(!s.VitVFragmentEligible(640));cases++;}
for(const char*a:{static_cast<const char*>(nullptr),"","1"}){reset();env("DLSS5_VIT_ADAPTIVE",a);S s;s.InitVitVFragment();assert(!s.VitVFragmentEligible(640));env("DLSS5_VIT_ADAPTIVE","0");assert(!s.VitVFragmentEligible(640));cases++;}
{reset();S s;s.InitVitVFragment();s.adaptive_active=true;assert(!s.VitVFragmentEligible(640));s.adaptive_active=false;assert(s.VitVFragmentEligible(640));s.fast_numeric=false;assert(!s.VitVFragmentEligible(640));cases++;}
{reset();S s;s.InitVitVFragment();s.W=1600;s.H=960;assert(s.VitVFragmentEligible(400));s.H=1024;assert(!s.VitVFragmentEligible(448));s.W=1920;s.H=1152;assert(s.VitVFragmentEligible(640));s.W=1280;s.H=768;assert(!s.VitVFragmentEligible(256));cases++;}
{reset();S s;s.InitVitVFragment();s.opt.graph=true;assert(!s.VitVFragmentEligible(640));s.opt.graph=false;s.opt.vit_byte_stream=true;assert(!s.VitVFragmentEligible(640));s.opt.vit_byte_stream=false;s.opt.skip_blocks.insert(31);assert(!s.VitVFragmentEligible(640));cases++;}
std::cout<<"PASS actual extracted production helpers: "<<cases<<" grouped default/priority/missing-export/scope/hot-state cases; HasFn is CPU stub, no GPU\n";}
