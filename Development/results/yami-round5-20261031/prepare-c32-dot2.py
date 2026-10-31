from pathlib import Path
import shutil
p=Path(__file__).parent;r=p/'c32-dot2/source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True);f=r/'hip/wave_owned_c32.inc';s=f.read_text();probe=(p/'guarded-dot2-probe.hip').read_text();a=probe.index('DEV float guarded_dot2');b=probe.index('extern "C"',a);helper=probe[a:b].replace('guarded_dot2','cw_exact_exp_sum64').replace('u4','cw_exact_u4')
helper='''// R5: ONLY positive Swin exponent halfs with low5fractionbitszero.
// den<2^(Emin+4) proves every positive FP32 partialsum exact; otherwise
// the caller retains the original4WMMA reduction. Never use for squared norms.
using cw_exact_u4=uint __attribute__((ext_vector_type(4)));
'''+helper
s=helper+s
a='''  f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);
  const float inv=norm_inverse(sums[0]);'''
b='''  uint cw_den_tier;float cw_den64=cw_exact_exp_sum64(ex,cw_den_tier);
  if(cw_den_tier==2u){f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
   _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);cw_den64=sums[0];}
  const float inv=norm_inverse(cw_den64);'''
assert s.count(a)==1;f.write_text(s.replace(a,b))
