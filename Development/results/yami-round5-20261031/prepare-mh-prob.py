from pathlib import Path
import shutil
p=Path(__file__).parent;r=p/'mh-prob/source'
shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True)
f=r/'hip/wave_owned_mh.inc';s=f.read_text()
anchor='DEV i2 w2_pack8(const float*t)'
helper='''// R5: probability-only bounded positive inputs. Retain fma-to-+0 products,
// remove only saturation MODE writes from this conversion site.
DEV i2 w2_pack8_probability(const float*t){
 int lo=__builtin_amdgcn_cvt_pk_fp8_f32(t[0],t[1],0,false);
 lo=__builtin_amdgcn_cvt_pk_fp8_f32(t[2],t[3],lo,true);
 int hi=__builtin_amdgcn_cvt_pk_fp8_f32(t[4],t[5],0,false);
 hi=__builtin_amdgcn_cvt_pk_fp8_f32(t[6],t[7],hi,true);return i2{lo,hi};}
'''
assert s.count(anchor)==1;s=s.replace(anchor,helper+anchor)
a='W2_Q8_SETF(a,e,float(exponent[key][e]),inv);}W2_Q8_END(a);prob[key]=a;'
b='W2_Q8_SETF(a,e,float(exponent[key][e]),inv);}a=w2_pack8_probability(a_t);prob[key]=a;'
assert s.count(a)==1;f.write_text(s.replace(a,b))
