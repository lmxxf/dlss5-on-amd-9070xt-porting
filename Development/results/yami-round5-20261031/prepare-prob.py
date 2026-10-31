from pathlib import Path
import shutil
p=Path(__file__).parent;r=p/'prob-direct/source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True)
f=r/'hip/wave_owned_c32.inc';s=f.read_text();anchor='''// Keep MODE and its four conversions indivisible: LLVM can hoist the FP8 intrinsics outside s_setreg.
''';new='''// Round5 isolated probability-only finite positive bounded path.
 if constexpr(Site==4){
  int lo=__builtin_amdgcn_cvt_pk_fp8_f32(t[0],t[1],0,false);
  lo=__builtin_amdgcn_cvt_pk_fp8_f32(t[2],t[3],lo,true);
  int hi=__builtin_amdgcn_cvt_pk_fp8_f32(t[4],t[5],0,false);
  hi=__builtin_amdgcn_cvt_pk_fp8_f32(t[6],t[7],hi,true);return i2{lo,hi};
 }
''';assert s.count(anchor)==1;f.write_text(s.replace(anchor,new+anchor))
