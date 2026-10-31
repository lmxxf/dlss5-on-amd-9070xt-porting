from pathlib import Path
import shutil
p=Path(__file__).parent;root=p/'fn-cache/source';shutil.copytree('src',root/'src',dirs_exist_ok=True);d=root/'Development/HIP';d.mkdir(parents=True,exist_ok=True)
for f in Path('Development/HIP').glob('*.h'):shutil.copyfile(f,d/f.name)
shutil.copyfile('Development/HIP/benchmark_live_capture.cpp',d/'benchmark_live_capture.cpp')
f=d/'hip_reference_network.h';s=f.read_text().replace('#include <type_traits>','#include <type_traits>\n#include <array>\n#include <string_view>')
anchor=' Handle Fn(const std::string&m,const std::string&name){'
helper=''' struct DirectFnEntry{size_t hash=0;std::string module,name;Handle handle=nullptr;};
#ifndef HIP_DIRECT_FN_CACHE_SLOTS
#define HIP_DIRECT_FN_CACHE_SLOTS 128
#endif
 static_assert((HIP_DIRECT_FN_CACHE_SLOTS&(HIP_DIRECT_FN_CACHE_SLOTS-1))==0);
 std::array<DirectFnEntry,HIP_DIRECT_FN_CACHE_SLOTS> direct_fn_cache{};
 unsigned long long direct_fn_hits=0,direct_fn_misses=0;
 Handle DirectFn(const std::string&m,const std::string&name){
  const size_t h=std::hash<std::string_view>{}(m)^(std::hash<std::string_view>{}(name)+0x9e3779b97f4a7c15ull);
  DirectFnEntry*empty=nullptr;
  for(size_t probe=0;probe<direct_fn_cache.size();++probe){auto&e=direct_fn_cache[(h+probe)&(direct_fn_cache.size()-1)];
   if(!e.handle){empty=&e;break;}
   if(e.hash==h&&e.module==m&&e.name==name){++direct_fn_hits;return e.handle;}
  }
  ++direct_fn_misses;std::string key=m+":"+name;auto it=functions.find(key);Handle f{};
  if(it!=functions.end())f=it->second;
  else{api.Check(api.hipModuleGetFunction(&f,modules.at(m),name.c_str()),name.c_str());functions.emplace(key,f);}
  if(empty){empty->hash=h;empty->module=m;empty->name=name;empty->handle=f;}
  return f;
 }
'''
assert s.count(anchor)==1;s=s.replace(anchor,helper+anchor)
old='  std::string target=name;\n  if(m=="c32_wave1"';new='  if(!(m=="c32_wave1"&&name.rfind("c32_wave1_",0)==0))return DirectFn(actual,name);\n  std::string target=name;\n  if(m=="c32_wave1"';assert s.count(old)==1;s=s.replace(old,new)
s=s.replace('~Network(){std::fprintf(stderr,','~Network(){std::fprintf(stderr,"DIRECT_FN_CACHE hits=%llu misses=%llu\\n",direct_fn_hits,direct_fn_misses);std::fprintf(stderr,',1);f.write_text(s)
