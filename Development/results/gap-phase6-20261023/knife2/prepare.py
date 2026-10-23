from pathlib import Path
import shutil,importlib.util,json,hashlib,difflib
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297'); W=Path('/home/lmxxf/work/dlss5-gap-phase6-20261023/knife2'); O=Path(__file__).parent
B=W/'baseline-source';A=W/'A/source'
assert not B.exists(), 'frozen baseline already exists'
for s in [B,A]:
 shutil.copytree(R/'hip',s/'hip');(s/'Development/HIP').mkdir(parents=True);shutil.copy2(R/'Development/HIP/swin_persistent_types.h',s/'Development/HIP/swin_persistent_types.h')
p=A/'hip/deep_fast.hip';text=p.read_text()
old='uint v=0;for(uint b=0;b<4;b++)v|=(uint(__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+b],0.f,0,false))&255u)<<(8*b);wv[q]=v;'
new='uint v=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4],r[q*4+1],0,false);wv[q]=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+2],r[q*4+3],v,true);'
assert text.count(old)==1;text=text.replace(old,new);p.write_text(text)
(O/'A.patch').write_text(''.join(difflib.unified_diff((B/'hip/deep_fast.hip').read_text().splitlines(True),text.splitlines(True),fromfile='a/hip/deep_fast.hip',tofile='b/hip/deep_fast.hip')))
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
g=O/'generated';g.mkdir(exist_ok=True);rows=[]
for role,s in [('baseline',B),('A',A)]:
 for name,compiler,generated,defs,sources,opts in m.recipe(s/'hip'):
  if 'deep_fast.hip' not in sources:continue
  p=g/(name+'.'+role+'.hip');p.write_text(generated);rows.append({'role':role,'name':name,'generated':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'compiler':compiler,'defines':defs,'sources':sources,'opts':opts})
d={'module_manifest_sha256':hashlib.sha256((R/'hip/SHA256SUMS').read_bytes()).hexdigest(),'header_sha256':hashlib.sha256((R/'Development/HIP/hip_reference_network.h').read_bytes()).hexdigest(),'rows':rows,'production_untouched':True}
(O/'identity.json').write_text(json.dumps(d,indent=2)+'\n');print([(r['role'],r['name']) for r in rows])
