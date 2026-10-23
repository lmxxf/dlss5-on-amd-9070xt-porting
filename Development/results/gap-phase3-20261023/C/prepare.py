import pathlib,shutil,subprocess,hashlib,json,importlib.util
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase3-20261023/C');O=pathlib.Path(__file__).parent;S=W/'source'
shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy2(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/c512_qkv_attention_compact.inc';s=p.read_text();needle='    _Pragma("unroll") for(uint j=0;j<6;j++)_Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cb[j],ca[t],accq[t][j]);'
# Exactly the W16 branch's group, not the non-W16 D-deep branch.
a=s.index('  if constexpr(W16){');pos=s.index(needle,a);assert pos<s.index('  }else{',pos)
new='''    // Phase3 C: the final W16 load belongs to j5. Schedule ready independent accumulators first.
    // K0..31 order within every accumulator is unchanged. Keep both outer scheduler fences and D4.
    if(s>=30){
     _Pragma("unroll") for(uint j=0;j<5;j++)_Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cb[j],ca[t],accq[t][j]);
     __builtin_amdgcn_sched_barrier(0);
     _Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][5]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cb[5],ca[t],accq[t][5]);
    }else{
'''+needle+'\n    }'
s=s[:pos]+new+s[pos+len(needle):];p.write_text(s)
d=subprocess.run(['diff','-u','--label','a/hip/c512_qkv_attention_compact.inc','--label','b/hip/c512_qkv_attention_compact.inc',str(R/'hip/c512_qkv_attention_compact.inc'),str(p)],capture_output=True,text=True).stdout;(O/'candidate.patch').write_text(d)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for role,root in [('baseline',R),('candidate',S)]:
 for name,_,src,defs,parts,opts in m.recipe(root/'hip'):
  if name=='c512-m32-mh':(W/(role+'.generated.hip')).write_text(src)
(O/'source-identity.json').write_text(json.dumps({'base':hashlib.sha256((R/'hip/c512_qkv_attention_compact.inc').read_bytes()).hexdigest(),'candidate':hashlib.sha256(p.read_bytes()).hexdigest()},indent=2));print(S)
