from pathlib import Path
import hashlib,shutil,importlib.util,json,subprocess
R=Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023');O=Path(__file__).parent
B=W/'baseline-source';S=W/'knife1/source'
if not B.exists():
 for d in ['hip','src','Development/HIP']:shutil.copytree(R/d,B/d)
for d in ['hip','src','Development/HIP']:shutil.copytree(B/d,S/d,dirs_exist_ok=True)
p=S/'hip/c512_qkv_attention_compact.inc';s=p.read_text()
prefix='''// Phase5 isolated switches. No production default changes.
#ifndef C512_PAIR_PACK
#define C512_PAIR_PACK 0
#endif
#ifndef C512_NORM_PERMLANE
#define C512_NORM_PERMLANE 0
#endif
'''
s=prefix+s
pos=s.index('DEV float c5c_serial_norm(')
helper='''#if C512_NORM_PERMLANE
DEV float c5c_norm_broadcast(float v,bool upper){
 uint b=__builtin_bit_cast(uint,v);
 uint swapped=__builtin_amdgcn_permlanex16(b,b,0x76543210u,0xfedcba98u,false,false);
 const bool high=(__builtin_amdgcn_workitem_id_x()&16u)!=0;
 return high==upper?v:__builtin_bit_cast(float,swapped);
}
#endif
'''
s=s[:pos]+helper+s[pos:]
for old,new in [('s=c5c_shuffle(s,row);','s=c5c_norm_broadcast(s,false);'),('s=c5c_shuffle(s,row+16);','s=c5c_norm_broadcast(s,true);'),('return c5c_shuffle(s,row+16);','return c5c_norm_broadcast(s,true);')]:
 assert old in s;s=s.replace(old,'\n#if C512_NORM_PERMLANE\n '+new+'\n#else\n '+old+'\n#endif\n')
pos=s.index('DEV void c5c_bits(')
helpers='''#if C512_PAIR_PACK
// Both arguments arrive after their original path-specific float operations.
DEV i2 c5c_pack8_pairs(f8 v){i2 a{};
 a[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[0],v[1],0,false);
 a[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[2],v[3],a[0],true);
 a[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[4],v[5],0,false);
 a[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[6],v[7],a[1],true);return a;}
DEV float c5c_pair_qkv_input(float a,float b){
#pragma clang fp contract(off)
 return __builtin_amdgcn_fmed3f(a*b+0.f,-448.f,448.f);}
DEV float c5c_pair_plain_input(float x){return __builtin_amdgcn_fmed3f(x,-448.f,448.f);}
#endif
'''
s=s[:pos]+helpers+s[pos:]
old='''    i2 a{};_Pragma("unroll 8") for(uint e=0;e<8;e++)
#if C512_COMPACT_NOF
     c5c_bits(a,e,c5c_q8mz(acc[t][ci][e],inv));
#else
     c5c_bits(a,e,q8(F(acc[t][ci][e]*inv)));
#endif'''
assert s.count(old)==1
new='''#if C512_PAIR_PACK
    f8 qv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){
#if C512_COMPACT_NOF
     qv[e]=c5c_pair_qkv_input(acc[t][ci][e],inv);
#else
     qv[e]=F(acc[t][ci][e]*inv);
#endif
    }i2 a=c5c_pack8_pairs(qv);
#else
'''+old+'\n#endif'
s=s.replace(old,new)
old='''   i2 a{};_Pragma("unroll 8") for(uint e=0;e<8;e++){float inv=c5c_inv(sums[0][e]+sums[1][e]);c5c_bits(a,e,c5c_fp8(float(exponent[key][e])*inv));}prob[key]=a;'''
assert s.count(old)==1
s=s.replace(old,'''#if C512_PAIR_PACK
   f8 pv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){float inv=c5c_inv(sums[0][e]+sums[1][e]);pv[e]=c5c_pair_plain_input(float(exponent[key][e])*inv);}prob[key]=c5c_pack8_pairs(pv);
#else
'''+old+'\n#endif')
old='''   i2 bytes{};_Pragma("unroll 8") for(uint e=0;e<8;e++)
#if C512_COMPACT_NOF
    c5c_bits(bytes,e,c5c_fp8(av[e]));
#else
    c5c_bits(bytes,e,c5c_fp8(c5c_F(av[e])));
#endif'''
assert s.count(old)==1
s=s.replace(old,'''#if C512_PAIR_PACK
   f8 bv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){
#if C512_COMPACT_NOF
    bv[e]=c5c_pair_plain_input(av[e]);
#else
    bv[e]=c5c_pair_plain_input(c5c_F(av[e]));
#endif
   }i2 bytes=c5c_pack8_pairs(bv);
#else
'''+old+'\n#endif')
p.write_text(s)
(O/'candidate.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/c512_qkv_attention_compact.inc','--label','b/hip/c512_qkv_attention_compact.inc',str(B/'hip/c512_qkv_attention_compact.inc'),str(p)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
base=next(x for x in m.recipe(B/'hip')if x[0]=='c512-m32-mh')[2];candidate=next(x for x in m.recipe(S/'hip')if x[0]=='c512-m32-mh')[2]
D=W/'knife1';D.mkdir(exist_ok=True);(D/'baseline.generated.hip').write_text(base)
for name,pack,perm in [('pair',1,0),('perm',0,1),('combined',1,1)]:
 (D/(name+'.generated.hip')).write_text(f'#define C512_PAIR_PACK {pack}\n#define C512_NORM_PERMLANE {perm}\n'+candidate)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
r={'frozen_phase4_header_sha256':sha(B/'Development/HIP/hip_reference_network.h'),'frozen_module_manifest_sha256':sha(B/'hip/SHA256SUMS'),'baseline_c512_source_sha256':sha(B/'hip/c512_qkv_attention_compact.inc'),'candidate_c512_source_sha256':sha(p),'generated':{p.name:sha(p)for p in D.glob('*.generated.hip')},'module':'c512-m32-mh','RTC_EXTRA_OPTS':'','GPU_not_run':True,'production_not_modified':True}
(O/'source-identity.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
