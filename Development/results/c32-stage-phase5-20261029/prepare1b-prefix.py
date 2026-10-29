from pathlib import Path
import shutil,difflib
p=Path(__file__).parent;d=p/'work/1b-prefix-source';assert not d.exists();shutil.copytree(p/'work/1b-source',d);f=d/'wave_owned_c32.inc';s=f.read_text();old=s
base=Path('hip/wave_owned_c32.inc').read_text();a=base.index('  else if constexpr(Prefix){');b=base.index('   for(uint ci=0;ci<2;ci++){',a);raw=base[a+len('  else if constexpr(Prefix){'):b]
a=s.index('  _Pragma("unroll") for(uint b=0;b<2;b++){uint qt=qb+b;auto&input=inputs[b];');b=s.index('  if constexpr(Chain)for(uint part=0;part<3;part++){',a);stage=s[a:b]
special='''  if constexpr(Prefix){
   h8 prefix_inputs[2];
   _Pragma("unroll") for(uint b=0;b<2;b++){uint qt=qb+b;
'''+raw+'''
    prefix_inputs[b]=a;
   }
   for(uint ci=0;ci<2;ci++){
    h8 bw{};float gain[8];for(uint e=0;e<8;e++)bw[e]=(_Float16)fw[(ci*16+r)*16+g*8+e];__builtin_memcpy(gain,fw+8704+ci*16+g*8,32);
    _Pragma("unroll") for(uint b=0;b<2;b++){
     f8 pre=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(bw,prefix_inputs[b],f8{});h8 zero{};
     pre=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(zero,zero,pre);
     CW_Q8_DECL(packed);for(uint e=0;e<8;e++){float v=Hrtz(pre[e]);CW_Q8_SET(packed,e,v);totals[b][ci][e]=cw_clampf(v,-448.f,448.f)*gain[e];}CW_Q8_END(packed);inputs[b][ci]=packed;
    }
   }
  }else{
'''+stage+'  }\n'
s=s[:a]+special+s[b:];s=s.replace('  if constexpr(Prefix)_Pragma("unroll") for(uint ci=0;ci<2;ci++)for(uint e=0;e<8;e++)prefix_weights[ci][e]=(_Float16)fw[(ci*16+r)*16+g*8+e];','').replace('if constexpr(!Chain)_Pragma("unroll")','if constexpr(!Chain&&!Prefix)_Pragma("unroll")');f.write_text(s);(p/'1b-prefix-lifetime.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/1b',tofile='b/1b-prefix')))
f=d/'build-modules.ps1';s=f.read_text().replace("'CW_QT_BATCH_MASK 31'","'CW_QT_BATCH_MASK 63'");f.write_text(s)
