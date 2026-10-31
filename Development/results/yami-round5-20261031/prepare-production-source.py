from pathlib import Path
import argparse,shutil,re,json
p=Path(__file__).parent;a=argparse.ArgumentParser();a.add_argument('--dotpar',action='store_true');a.add_argument('--norm900',action='store_true');a.add_argument('--c256',action='store_true');o=a.parse_args();name='production-combined'+('-norm900'if o.norm900 else'-dot'if o.dotpar else'')+('-c256'if o.c256 else'');r=p/name/'source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True);hip=r/'hip'
# ViT: only the reviewed fast packed module on gfx1201; legacy scalar path stays intact.
f=hip/'deep_fast.hip';s=f.read_text();old='''  for(uint e=0;e<8;e++){
   float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);
   union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;'''
new='''#if defined(__gfx1201__) && HIP_VIT_PAIR_EXP_MAP
  uint pair_exp[4];
  for(uint e=0;e<4;e++){
   float af0=clampf(a[2*e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   float af1=clampf(a[2*e+1]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   pair_exp[e]=(__builtin_amdgcn_perm(bits(af1),bits(af0),0x06050201u)>>1)&0x3ff03ff0u;
  }
  x=__builtin_bit_cast(h8,pair_exp);
#endif
  for(uint e=0;e<8;e++){
#if defined(__gfx1201__) && HIP_VIT_PAIR_EXP_MAP
   // COMGR can read element0 when bit_cast is applied directly to an h8 component lvalue.
   unsigned short u=static_cast<unsigned short>(pair_exp[e/2]>>(16*(e%2)));
   union{unsigned short u;_Float16 h;}cv;cv.u=u;
#else
   float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);
   union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;
#endif'''
assert s.count(old)==2;s=s.replace(old,new,1);s='#ifndef HIP_VIT_PAIR_EXP_MAP\n#define HIP_VIT_PAIR_EXP_MAP 0\n#endif\n'+s;f.write_text(s)
# C512: only probability values, leaving AV and all other clamp sites unchanged.
f=hip/'c512_qkv_attention_compact.inc';s=f.read_text();anchor='DEV float c5c_pair_plain_input(float x){return __builtin_amdgcn_fmed3f(x,-448.f,448.f);}';assert s.count(anchor)==1;s=s.replace(anchor,anchor+'''
#ifndef C512_PROB_UNCLAMP
#define C512_PROB_UNCLAMP 0
#endif
DEV float c5c_probability_input(float x){
#if defined(__gfx1201__) && C512_PROB_UNCLAMP
 return x;
#else
 return c5c_pair_plain_input(x);
#endif
}''');old='pv[e]=c5c_pair_plain_input(float(exponent[key][e])*inv);';assert s.count(old)==1;s=s.replace(old,'pv[e]=c5c_probability_input(float(exponent[key][e])*inv);');f.write_text(s)
flags={'deep_fast-packed-fast':'HIP_VIT_PAIR_EXP_MAP 1','c512-m32-mh':'C512_PROB_UNCLAMP 1'}
if o.dotpar or o.norm900:
 f=hip/'wave_owned_c32.inc';base=f.read_text();variant=(p/'c32-dotpar/source/hip/wave_owned_c32.inc').read_text();anchor='#ifndef CW_OUTPROJ_HOIST';helper=variant[:variant.index(anchor)];before='''  f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);
  const float inv=norm_inverse(sums[0]);''';start=variant.index('  uint cw_den_tier;');end=variant.index('\n  i2 prob[4];',start);after=variant[start:end];assert base.count(before)==1;base=base.replace(before,'#if defined(__gfx1201__) && CW_EXACT_DEN_SUM\n'+after+'\n#else\n'+before+'\n#endif');f.write_text('#ifndef CW_EXACT_DEN_SUM\n#define CW_EXACT_DEN_SUM 0\n#endif\n#if defined(__gfx1201__) && CW_EXACT_DEN_SUM\n'+helper+'#endif\n'+base);flags.update({'c32-wave1-fast-norm900':'CW_EXACT_DEN_SUM 1'});flags.update({'c32-wave1-fast':'CW_EXACT_DEN_SUM 1'}if o.dotpar and not o.norm900 else{})
if o.c256:
 f=hip/'wave_owned_mh.inc';base=f.read_text();variant=(p/'one-plane-c256-rc/source/hip/wave_owned_mh.inc').read_text();start=variant.index('template<uint C,bool ByteIn,bool ByteOut,bool Up=false',variant.index('#define W2_KERNEL'));end=variant.index('#if !defined(W2_NO_EXPORTS)',start);body=variant[start:end]
 first=re.search(r'^#define W2_KERNEL\(.*$',base,re.M);active=re.search(r'^#define W2_KERNEL\(.*$',variant,re.M)[0];assert first
 base=base[:first.end()]+'\n#if defined(__gfx1201__) && W2_SINGLE_PLANE_C256\n'+body+'\n#undef W2_KERNEL\n'+active+'\n#endif\n'+base[first.end():]
 for macro in ['W2_KERNEL_W16','W2_KERNEL_HO','W2_KERNEL_HOB']:
  m=re.search(r'^#define '+macro+r'\(.*$',base,re.M);v=re.search(r'^#define '+macro+r'\(.*$',variant,re.M)[0];assert m;base=base[:m.start()]+'#if defined(__gfx1201__) && W2_SINGLE_PLANE_C256\n'+v+'\n#else\n'+m[0]+'\n#endif'+base[m.end():]
 f.write_text('#ifndef W2_SINGLE_PLANE_C256\n#define W2_SINGLE_PLANE_C256 0\n#endif\n'+base);flags.update({'c64-wave2':'W2_SINGLE_PLANE_C256 1','c64-wave2-fast':'W2_SINGLE_PLANE_C256 1'})
f=hip/'build-modules.ps1';s=f.read_text(encoding='utf-8-sig')
for mod,flag in flags.items():
 pat=r"(@\{ name\s*=\s*'"+re.escape(mod)+r"';\s*defines\s*=\s*@\()(.*?)(\);\s*sources)";m=re.search(pat,s);assert m,mod;s=s[:m.start()]+m[1]+m[2]+(","if m[2].strip()else'')+"'"+flag+"'"+m[3]+s[m.end():]
anchor="    $opts = (@($(if ($RowOpts) { $m.opts }), $ExtraOpts) | Where-Object { $_ }) -join ' '"
assert s.count(anchor)==1
s=s.replace(anchor,"    # Verified production split: max-ilp for gfx1200 C512 deep, default for gfx1201.\n    $moduleBackendOptions = $m.opts\n    if ($target -eq 'gfx1201' -and $m.name -eq 'c512-m32-deep') { $moduleBackendOptions = '' }\n    $opts = (@($(if ($RowOpts) { $moduleBackendOptions }), $ExtraOpts) | Where-Object { $_ }) -join ' '")
f.write_text(s);(p/name/'source-plan.json').write_text(json.dumps(dict(baseline='b60e6a8c',gfx1201_only=True,dotpar=o.dotpar,norm900=o.norm900,c256=o.c256,recipe_flags=flags,root=str(r)),indent=2)+'\n');print(r)
