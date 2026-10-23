#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <vector>
#include <iostream>
// Exercise the guarded KS2 branch as well; production builds may disable this macro.
#define HIP_VIT_ATTN_KSPLIT_HOST 1
using U=unsigned;
struct Opt{bool graph=false,vit_byte_stream=false,vit_half_stream=false,vit_qkv_fused=true,vit_qkv_frag=true,vit_qkv_fp8=true,vit_attn_fused=true;std::set<U>skip_blocks;std::string dup_prefix,dump_dir;};
struct S{Opt opt;U W=1920,H=1088,multi_pass=1,vit_stream_active=3;bool small_active=false,fast_numeric=true,adaptive_active=false,observer=false,vit_contract_byte_edge=true;std::set<std::string>missing,ks2;std::vector<std::string>probes;bool HasFn(const std::string&m,const std::string&n){probes.push_back(m+":"+n);if(n.size()>4&&n.substr(n.size()-4)=="_ks2")return ks2.count(n);return !missing.count(n);}
 bool vfrag_requested=false,vfrag_available=false,vfrag_ae_off=false;unsigned long long vfrag_producer_calls=0,vfrag_consumer_calls=0;
 void InitVitVFragment(){const char*v=std::getenv("NR_VIT_VFRAG");if(!v||!*v)v=std::getenv("DLSS5_HIP_VIT_VFRAG");vfrag_requested=!v||!*v||std::strcmp(v,"1")==0;const char*ae=std::getenv("DLSS5_VIT_ADAPTIVE");vfrag_ae_off=ae&&std::strcmp(ae,"0")==0;if(vfrag_requested)vfrag_available=HasFn("vit_stream","vit_stream_qkv_frag_bin_w5f8_vfrag")&&HasFn("deep_fast","vit_attention_fused_400_bytein_bout_vfrag")&&HasFn("deep_fast","vit_attention_fused_640_bytein_bout_vfrag");}
 bool VitVFragmentEligible(U n){
  // Phase5 knife2: only admission expands; reuse_gate, AE decisions/anchors and GPU byte layout stay unchanged.
  const bool shape=small_active?
   ((W==1600&&H==1024&&n==448)||(W==1344&&H==768&&n==288)||(W==1920&&(H==1088||H==1152)&&n==640)):
   ((W==1600&&H==960&&n==400)||(W==1920&&(H==1088||H==1152)&&n==640));
  if(!vfrag_requested||!vfrag_available||!shape||!fast_numeric||multi_pass!=1||opt.graph||!opt.skip_blocks.empty()||!opt.dup_prefix.empty()||observer||!opt.dump_dir.empty()||opt.vit_byte_stream||opt.vit_half_stream||vit_stream_active!=3||!vit_contract_byte_edge||!opt.vit_qkv_fused||!opt.vit_qkv_frag||!opt.vit_qkv_fp8||!opt.vit_attn_fused)return false;
  if(HIP_VIT_ATTN_KSPLIT_HOST&&HasFn("deep_fast",std::string(n<=400?"vit_attention_fused_400_bytein_bout":"vit_attention_fused_640_bytein_bout")+"_ks2"))return false;
  return true;
 }
};
void env(const char*n,const char*v){if(v)setenv(n,v,1);else unsetenv(n);}
void reset(){env("NR_VIT_VFRAG",nullptr);env("DLSS5_HIP_VIT_VFRAG",nullptr);env("DLSS5_VIT_ADAPTIVE",nullptr);}
int main(){unsigned checks=0;
for(bool small:{false,true})for(unsigned W:{1280u,1344u,1600u,1920u})for(unsigned H:{768u,960u,1024u,1088u,1152u})for(unsigned n:{240u,288u,400u,448u,640u})for(bool active:{false,true}){
 reset();env("DLSS5_VIT_ADAPTIVE",active?"1":"0");S s;s.W=W;s.H=H;s.small_active=small;s.adaptive_active=active;s.InitVitVFragment();
 bool yes=small?((W==1344&&H==768&&n==288)||(W==1600&&H==1024&&n==448)||(W==1920&&(H==1088||H==1152)&&n==640)):((W==1600&&H==960&&n==400)||(W==1920&&(H==1088||H==1152)&&n==640));
 assert(s.VitVFragmentEligible(n)==yes);++checks;
}
for(unsigned n:{288u,448u}){reset();S s;s.small_active=true;s.W=n==288?1344:1600;s.H=n==288?768:1024;s.InitVitVFragment();assert(s.VitVFragmentEligible(n));const std::string k=n<=400?"vit_attention_fused_400_bytein_bout_ks2":"vit_attention_fused_640_bytein_bout_ks2";s.ks2.insert(k);assert(!s.VitVFragmentEligible(n));++checks;}
for(auto name:{"vit_stream_qkv_frag_bin_w5f8_vfrag","vit_attention_fused_400_bytein_bout_vfrag","vit_attention_fused_640_bytein_bout_vfrag"}){reset();S s;s.missing.insert(name);s.InitVitVFragment();assert(!s.vfrag_available&&!s.VitVFragmentEligible(640));++checks;}
{reset();S s;s.InitVitVFragment();assert(s.VitVFragmentEligible(640));s.multi_pass=2;assert(!s.VitVFragmentEligible(640));s.multi_pass=1;s.fast_numeric=false;assert(!s.VitVFragmentEligible(640));s.fast_numeric=true;s.opt.graph=true;assert(!s.VitVFragmentEligible(640));++checks;}
{reset();env("NR_VIT_VFRAG","0");env("DLSS5_HIP_VIT_VFRAG","1");S s;s.InitVitVFragment();assert(!s.VitVFragmentEligible(640));++checks;}
std::cout<<"PASS "<<checks<<" actual-helper CPU admission checks; AE policies are not executed or modified\n";
}
