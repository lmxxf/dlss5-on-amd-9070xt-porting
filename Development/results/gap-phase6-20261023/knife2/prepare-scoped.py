from pathlib import Path
import shutil,importlib.util,json,hashlib,difflib
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=Path('/home/lmxxf/work/dlss5-gap-phase6-20261023/knife2');O=Path(__file__).parent
B=W/'baseline-source';S=W/'A/scoped-source';assert not S.exists();shutil.copytree(B,S)
p=S/'hip/deep_fast.hip';s=p.read_text()
old='uint v=0;for(uint b=0;b<4;b++)v|=(uint(__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+b],0.f,0,false))&255u)<<(8*b);wv[q]=v;'
new='uint v=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4],r[q*4+1],0,false);wv[q]=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+2],r[q*4+3],v,true);'
assert s.count(old)==1;s=s.replace(old,'\n#if HIP_UP_WIDE_BYTE_PAIR\n'+new+'\n#else\n'+old+'\n#endif\n');s='#ifndef HIP_UP_WIDE_BYTE_PAIR\n#define HIP_UP_WIDE_BYTE_PAIR 0\n#endif\n'+s;p.write_text(s)
p=S/'hip/build-modules.ps1';s=p.read_text();old="@{ name = 'deep_fast-packed-fast';                   defines = @(";assert s.count(old)==1;s=s.replace(old,old+"'HIP_UP_WIDE_BYTE_PAIR 1',");p.write_text(s)
patch=''
for f in ['hip/deep_fast.hip','hip/build-modules.ps1']:patch+=''.join(difflib.unified_diff((B/f).read_text().splitlines(True),(S/f).read_text().splitlines(True),fromfile='a/'+f,tofile='b/'+f))
(O/'A-scoped.patch').write_text(patch)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
g=O/'generated-scoped';g.mkdir();rows=[]
for name,compiler,generated,defs,sources,opts in m.recipe(S/'hip'):
 if 'deep_fast.hip' not in sources:continue
 p=g/(name+'.A.hip');p.write_text(generated);rows.append({'name':name,'generated':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'pair_enabled':'HIP_UP_WIDE_BYTE_PAIR 1' in defs})
assert [x['name'] for x in rows if x['pair_enabled']]==['deep_fast-packed-fast']
(O/'A-scoped-identity.json').write_text(json.dumps({'source':str(S),'rows':rows,'production_changed':False,'prior_unscoped_source_preserved':str(W/'A/source')},indent=2)+'\n')
