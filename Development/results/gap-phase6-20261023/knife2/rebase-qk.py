from pathlib import Path
import shutil,importlib.util,json,hashlib,difflib
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=Path('/home/lmxxf/work/dlss5-gap-phase6-20261023/knife2/A/qk-rebase');O=Path(__file__).parent
assert not W.exists();B=W/'baseline-source';S=W/'candidate-source'
for d in [B,S]:
 shutil.copytree(R/'hip',d/'hip');(d/'Development/HIP').mkdir(parents=True)
 for f in ['swin_persistent_types.h','hip_reference_network.h']:shutil.copy2(R/'Development/HIP'/f,d/'Development/HIP'/f)
p=S/'hip/deep_fast.hip';s=p.read_text();old='uint v=0;for(uint b=0;b<4;b++)v|=(uint(__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+b],0.f,0,false))&255u)<<(8*b);wv[q]=v;';new='uint v=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4],r[q*4+1],0,false);wv[q]=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+2],r[q*4+3],v,true);'
assert s.count(old)==1 and 'HIP_UP_WIDE_BYTE_PAIR' not in s
s=s.replace(old,'\n#if HIP_UP_WIDE_BYTE_PAIR\n'+new+'\n#else\n'+old+'\n#endif\n');s='#ifndef HIP_UP_WIDE_BYTE_PAIR\n#define HIP_UP_WIDE_BYTE_PAIR 0\n#endif\n'+s;p.write_text(s)
p=S/'hip/build-modules.ps1';s=p.read_text();old="@{ name = 'deep_fast-packed-fast';                   defines = @(";assert s.count(old)==1;s=s.replace(old,old+"'HIP_UP_WIDE_BYTE_PAIR 1',");p.write_text(s)
patch=''
for f in ['hip/deep_fast.hip','hip/build-modules.ps1']:patch+=''.join(difflib.unified_diff((B/f).read_text().splitlines(True),(S/f).read_text().splitlines(True),fromfile='a/'+f,tofile='b/'+f))
(O/'A-qk-rebased.patch').write_text(patch)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
g=O/'generated-qk-rebased';g.mkdir();rows=[]
for role,d in [('baseline',B),('candidate',S)]:
 for name,compiler,generated,defs,sources,opts in m.recipe(d/'hip'):
  if name!='deep_fast-packed-fast':continue
  p=g/(name+'.'+role+'.hip');p.write_text(generated)
  exports=['vit_attention_fused_400_bytein_bout_qkfrag','vit_attention_fused_640_bytein_bout_qkfrag'];assert all(generated.count('void '+x+'(')==1 for x in exports)
  rows.append({'role':role,'name':name,'source':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'defines':defs,'opts':opts,'preserved_qk_exports':exports})
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(O/'A-qk-rebased-identity.json').write_text(json.dumps({'work':str(W),'header_sha256':h(R/'Development/HIP/hip_reference_network.h'),'production_deep_sha256':h(R/'hip/deep_fast.hip'),'module_manifest_sha256':h(R/'hip/SHA256SUMS'),'rows':rows,'production_changed':False,'host':'reuse accepted limited-QK host'},indent=2)+'\n')
print(json.dumps(rows,indent=2))
