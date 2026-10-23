from pathlib import Path
import shutil,importlib.util,json,hashlib,difflib
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=Path('/home/lmxxf/work/dlss5-gap-phase6-20261023/knife2/A');O=Path(__file__).parent
B=W/'qk-rebase/baseline-source';A=W/'qk-rebase/candidate-source';S=W/'half-only-source';assert not S.exists();shutil.copytree(A,S)
p=S/'hip/deep_fast.hip';s=p.read_text();pair='uint v=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4],r[q*4+1],0,false);wv[q]=__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+2],r[q*4+3],v,true);';scalar='uint v=0;for(uint b=0;b<4;b++)v|=(uint(__builtin_amdgcn_cvt_pk_fp8_f32(r[q*4+b],0.f,0,false))&255u)<<(8*b);wv[q]=v;';old='#if HIP_UP_WIDE_BYTE_PAIR\n'+pair+'\n#else\n'+scalar+'\n#endif';new='#if HIP_UP_WIDE_BYTE_PAIR\nif constexpr(!W8){'+pair+'}else{'+scalar+'}\n#else\n'+scalar+'\n#endif';assert s.count(old)==1;s=s.replace(old,new);p.write_text(s)
patch=''
for f in ['hip/deep_fast.hip','hip/build-modules.ps1']:patch+=''.join(difflib.unified_diff((B/f).read_text().splitlines(True),(S/f).read_text().splitlines(True),fromfile='a/'+f,tofile='b/'+f))
(O/'A-half-only.patch').write_text(patch)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for name,compiler,generated,defs,sources,opts in m.recipe(S/'hip'):
 if name!='deep_fast-packed-fast':continue
 p=O/'generated-qk-rebased/deep_fast-packed-fast.half-only.hip';p.write_text(generated)
 assert all('void '+n+'(' in generated for n in ['vit_attention_fused_400_bytein_bout_qkfrag','vit_attention_fused_640_bytein_bout_qkfrag'])
 d={'source_root':str(S),'generated':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'defines':defs,'opts':opts,'scope':'ByteOutput && !W8; no resolution condition; W8 including W16 scalar','production_changed':False}
 (O/'A-half-only-identity.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps(d,indent=2))
