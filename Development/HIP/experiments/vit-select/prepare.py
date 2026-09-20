from pathlib import Path
import shutil,difflib
here=Path(__file__).resolve().parent;root=here.parents[3];out=Path('/tmp/vit-select-src')
for p in ['src','Development/HIP']:
 shutil.copytree(root/p,out/p,dirs_exist_ok=True)
k=out/'kernel';k.mkdir(exist_ok=True);(k/'deep_fast.hip').write_text((root/'hip/deep_fast.hip').read_text()+'\n'+(here/'select.hip').read_text())
p=out/'Development/HIP/hip_reference_network.h';old=p.read_text();s=old
method=''' std::set<U>select_logged;
 void SelectVit(Tensor norm,Tensor av,U n,const std::string&fused,U block){
  const char*setting=std::getenv("DLSS5_VIT_SELECT_THRESHOLD");
  if(!setting||!*setting){Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);return;}
  if(!opt.vit_qkv_fp8||opt.vit_byte_stream||(n!=400&&n!=640))throw std::runtime_error("select experiment needs FP8 QKV, f32 output, 900/1080");
  float threshold=std::stof(setting);U groups=(n+31)/32;auto pool=New(size_t(n)*256),route=New(size_t(groups)*64);
  Run("deep","vit_select_prep",size_t(groups)*32*256,P(norm),P(pool),P(route),n,threshold);
  Run("deep",n==400?"vit_attention_select_400":"vit_attention_select_640",size_t(n)*512,P(norm),P(pool),P(route),P(av),n);
  if(const char*path=std::getenv("DLSS5_VIT_SELECT_DIAG");path&&*path&&!select_logged.count(block)){
   Synchronize();std::vector<U>r(size_t(groups)*64);api.Check(api.hipMemcpy(r.data(),P(route),r.size()*4,2),"select route read");
   FILE*f=fopen(path,"ab");if(!f)throw std::runtime_error("select diagnostics open");
   for(U h=0;h<32;h++)for(U g=0;g<groups;g++){float distance;auto at=(h*groups+g)*2;memcpy(&distance,&r[at+1],4);fprintf(f,"%u,%u,%u,%u,%u,%.9g,%.9g\\n",n,block,h,g,r[at],distance,threshold);}fclose(f);select_logged.insert(block);
  }
 }
'''
s=s.replace(' Tensor Vit(Tensor input,U n,U block){',method+' Tensor Vit(Tensor input,U n,U block){',1)
needle='if(opt.vit_qkv_fp8)fused+="_bytein";Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);'
assert s.count(needle)==1
s=s.replace(needle,'if(opt.vit_qkv_fp8)fused+="_bytein";SelectVit(norm,av,n,fused,block);');p.write_text(s)
(here/'host.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/Development/HIP/hip_reference_network.h',tofile='b/Development/HIP/hip_reference_network.h')))
print(out)
