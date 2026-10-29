from pathlib import Path
import shutil,difflib
p=Path(__file__).parent;d=p/'work/1b-source';assert not d.exists();shutil.copytree(p/'work/1a-source',d);f=d/'wave_owned_c32.inc';old=f.read_text();s='#ifndef CW_QT_BATCH_MASK\n#define CW_QT_BATCH_MASK 127\n#endif\n#ifndef CW_QT_BATCH\n#define CW_QT_BATCH 1\n#endif\n'+old
a=s.index('#if CW_ROLL_WINDOW\n',s.index('DEV void cw_body'));z=s.index(' cw_i2_pair proj_hoisted',a);orig=s[a:z]
lo=orig.index('  if constexpr(Chain){for(uint ci=0;ci<2;ci++)input[ci]=cw_input');hi=orig.index('  if constexpr(Chain){\n   if constexpr(W16==1 && CW_DIAG_ONLY)');stage=orig[lo:hi]
stage=stage.replace('h8 b{};for(uint e=0;e<8;e++)b[e]=(_Float16)fw[(ci*16+r)*16+g*8+e];','h8 b=prefix_weights[ci];').replace('fw[8704+ci*16+g*8+e]','batch_gain[ci][e]').replace('__builtin_memcpy(fv,fw+8704+c0,32);','__builtin_memcpy(fv,batch_gain[ci],32);').replace('__builtin_memcpy(res,__builtin_assume_aligned(fw+8704+c0,32),32);','__builtin_memcpy(res,batch_gain[ci],32);')
acta=orig.index('#if CW_FAST_NUM&4',orig.index('for(uint h2=0;h2<4;h2++)'));actb=orig.index('     act[hp]=a;',acta);act=orig[acta:actb]
qa=orig.index('  for(uint part=0;part<3;part++){',orig.index('  i2 feature[2]'));norma=orig.index('   f8 sum{};',qa);normb=orig.index('\n#if CW_QKV_FENCE\n   __builtin_amdgcn_sched_barrier(0);',norma);norm=orig[norma:normb]
batch=''' if constexpr(CW_QT_BATCH==2 && W16==1 && CW_DIAG_ONLY && (CW_QT_BATCH_MASK&(1u<<(Up?6:cw_kind)))){
 _Pragma("clang loop unroll(disable)") for(uint qb=0;qb<4;qb+=2){
  const uint lane=__builtin_amdgcn_workitem_id_x(),r=lane&15u,g=lane>>4;
  i2 inputs[2][2];f8 totals[2][2]{};h8 prefix_weights[2];float batch_gain[2][8];
  if constexpr(Prefix)_Pragma("unroll") for(uint ci=0;ci<2;ci++)for(uint e=0;e<8;e++)prefix_weights[ci][e]=(_Float16)fw[(ci*16+r)*16+g*8+e];
  if constexpr(!Chain)_Pragma("unroll") for(uint ci=0;ci<2;ci++)__builtin_memcpy(batch_gain[ci],fw+8704+ci*16+g*8,32);
  _Pragma("unroll") for(uint b=0;b<2;b++){uint qt=qb+b;auto&input=inputs[b];auto&ffn=totals[b];
'''+stage+'''
  }
  if constexpr(Chain)for(uint part=0;part<3;part++){
   cw_i2_pair bp=cw_matrix16(fw,34944+part*512+(g*16+r)*16);
   _Pragma("unroll") for(uint b=0;b<2;b++){
    totals[b][0]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(bp.lo,inputs[b][0],totals[b][0]);
    totals[b][1]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(bp.hi,inputs[b][1],totals[b][1]);
   }
  }
#if CW_ROLL_HIDDEN
  _Pragma("clang loop unroll(disable)")
#endif
  for(uint h2=0;h2<4;h2++){
   i2 acts[2][2];
   _Pragma("unroll") for(uint hp=0;hp<2;hp++){
    uint ht=h2*2+hp;cw_i2_pair bp=cw_matrix16(fw+512,ht*512+(g*16+r)*16);
    _Pragma("unroll") for(uint b=0;b<2;b++){
     f8 expanded=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(bp.lo,inputs[b][0],f8{});
     expanded=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(bp.hi,inputs[b][1],expanded);
'''+act+'''
     acts[b][hp]=a;
    }
   }
   for(uint ci=0;ci<2;ci++){
    cw_i2_pair cp=cw_matrix16(fw+4608,(ci*4+h2)*512+(g*16+r)*16);
    _Pragma("unroll") for(uint b=0;b<2;b++){
     totals[b][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cp.lo,acts[b][0],totals[b][ci]);
     totals[b][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cp.hi,acts[b][1],totals[b][ci]);
    }
   }
  }
  i2 features[2][2];_Pragma("unroll") for(uint b=0;b<2;b++)for(uint ci=0;ci<2;ci++){
   uint qt=qb+b;CW_Q8_DECL(a);
#if CW_RTZ_PAIR
   h8 rounded=cw_rtz_half8(totals[b][ci]);__builtin_memcpy(residual+((qt*2+ci)*32+(g*16+r))*8,&rounded,16);
   for(uint e=0;e<8;e++)CW_Q8_SET(a,e,float(rounded[e]));
#else
   for(uint e=0;e<8;e++){float v=Hrtz(totals[b][ci][e]);residual[((qt*2+ci)*32+(g*16+r))*8+e]=(_Float16)v;CW_Q8_SET(a,e,v);}
#endif
   CW_Q8_END_SITE(a,2);features[b][ci]=a;
  }
  for(uint part=0;part<3;part++){
   uint qlane=__builtin_amdgcn_workitem_id_x(),qr=qlane&15u,qg=qlane>>4;f8 qs[2][2]{};
   for(uint ci=0;ci<2;ci++){
    cw_i2_pair bp=cw_matrix16(w,(part*2+ci)*512+(qg*16+qr)*16);
    _Pragma("unroll") for(uint b=0;b<2;b++){
     if(part<2){qs[b][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(bp.lo,features[b][0],qs[b][ci]);qs[b][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(bp.hi,features[b][1],qs[b][ci]);}
     else{qs[b][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(features[b][0],bp.lo,qs[b][ci]);qs[b][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(features[b][1],bp.hi,qs[b][ci]);}
    }
   }
   _Pragma("unroll") for(uint b=0;b<2;b++){uint qt=qb+b;auto&q=qs[b];
'''+norm+'''
   }
  }
  __builtin_amdgcn_sched_barrier(0);
 }
 }else{
'''+orig+''' }
'''
s=s[:a]+batch+s[z:];f.write_text(s);(p/'1b-source.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/1a/wave_owned_c32.inc',tofile='b/1b/wave_owned_c32.inc')))
f=d/'build-modules.ps1';s=f.read_text().replace("'CW_OUTPROJ_HOIST 1',","'CW_QT_BATCH 2','CW_OUTPROJ_HOIST 1',");f.write_text(s)
