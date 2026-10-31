from pathlib import Path
import shutil
p=Path(__file__).parent;root=p/'prob-asm/source';shutil.copytree(p/'r2d-both/source',root,dirs_exist_ok=True);f=root/'hip/wave_owned_c32.inc';s=f.read_text();anchor='// Keep MODE and its four conversions indivisible: LLVM can hoist the FP8 intrinsics outside s_setreg.\n'
new='''// Isolated probability site: preserve conversion order and constraints; remove only MODE writes.
 if constexpr(Site==4){int lo,hi;
  asm volatile("v_cvt_pk_fp8_f32 %0, %2, %3\\n\\t"
               "v_cvt_pk_fp8_f32 %1, %6, %7\\n\\t"
               "v_cvt_pk_fp8_f32 %0, %4, %5 op_sel:[0,0,1]\\n\\t"
               "v_cvt_pk_fp8_f32 %1, %8, %9 op_sel:[0,0,1]"
               : "=&v"(lo),"=&v"(hi) : "v"(t[0]),"v"(t[1]),"v"(t[2]),"v"(t[3]),"v"(t[4]),"v"(t[5]),"v"(t[6]),"v"(t[7]) : "memory");
  return i2{lo,hi};}
''';assert s.count(anchor)==1;f.write_text(s.replace(anchor,new+anchor))
