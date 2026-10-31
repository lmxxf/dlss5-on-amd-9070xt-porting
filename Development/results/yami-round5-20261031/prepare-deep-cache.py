from pathlib import Path
import shutil
p=Path(__file__).parent;root=p/'deep-cache/source';shutil.copytree(p/'pair-qualified/source',root,dirs_exist_ok=True);f=root/'Development/HIP/hip_reference_network.h';s=f.read_text().replace('#include <type_traits>','#include <type_traits>\n#include <unordered_map>')
anchor=' Handle Fn(const std::string&m,const std::string&name){';helper=''' bool r5_deep_cache_on=false;std::unordered_map<std::string,Handle> r5_deep_functions;unsigned long long r5_deep_hits=0,r5_deep_misses=0;
 void InitR5DeepCache(){const char*v=std::getenv("NR_R5_DEEP_CACHE");r5_deep_cache_on=v&&std::strcmp(v,"1")==0&&W==1920&&H==1152&&fast_numeric&&!opt.integration.select_module&&CompactVitTailDeviceEligible();}
 bool R5DeepCacheEligible()const{return r5_deep_cache_on&&small_active&&multi_pass==1&&vit_exp_pair_ae0&&!opt.graph&&!opt.experimental_temporal&&!temporal_feature_tap_active&&!pulse_frame_history&&!native_post_output&&!observer&&opt.dump_dir.empty();}
 Handle R5DeepFn(const std::string&name){auto it=r5_deep_functions.find(name);if(it!=r5_deep_functions.end()){++r5_deep_hits;return it->second;}++r5_deep_misses;Handle f{};
  if(vit_exp_pair_fn&&name=="vit_attention_fused_640_bytein_bout_qkfrag"&&VitExpPairEligible(640)){f=vit_exp_pair_fn;}
  else{std::string key="deep_fast:"+name;auto k=functions.find(key);if(k!=functions.end())f=k->second;else{api.Check(api.hipModuleGetFunction(&f,modules.at("deep_fast"),name.c_str()),name.c_str());functions.emplace(key,f);}}
  r5_deep_functions.emplace(name,f);return f;
 }
''';assert s.count(anchor)==1;s=s.replace(anchor,helper+anchor+'\n  if(m=="deep_fast"&&R5DeepCacheEligible()){if(vit_exp_pair_fn&&name=="vit_attention_fused_640_bytein_bout_qkfrag"&&VitExpPairEligible(640))++vit_exp_pair_calls;return R5DeepFn(name);}')
s=s.replace('InitVitQKFragment();InitVitExpPair();','InitVitQKFragment();InitVitExpPair();InitR5DeepCache();')
s=s.replace('~Network(){std::fprintf(stderr,','~Network(){std::fprintf(stderr,"R5_DEEP_CACHE enabled=%u hits=%llu misses=%llu\\n",unsigned(r5_deep_cache_on),r5_deep_hits,r5_deep_misses);std::fprintf(stderr,',1);f.write_text(s)
