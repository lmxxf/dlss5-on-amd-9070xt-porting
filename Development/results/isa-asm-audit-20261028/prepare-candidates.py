from pathlib import Path
import shutil,subprocess,importlib.util,json
R=Path.cwd();O=R/'Development/results/isa-asm-audit-20261028';W=O/'work'
for role in ['upwide','dectail']:
 S=W/(role+'-source');shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=W/'upwide-source/hip/wave_owned_mh.inc';s=p.read_text();old='for(uint e=0;e<8;e++){float v;if constexpr(LowByte)v=__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[base+k+gr*8+e]),0);else v=in[base+k+gr*8+e];a[e]=ok?(_Float16)v:(_Float16)0.f;}'
new='''uint packed[2];if constexpr(LowByte)__builtin_memcpy(packed,reinterpret_cast<const unsigned char*>(in)+base+k+gr*8,8);for(uint e=0;e<8;e++){float v;if constexpr(LowByte)v=__builtin_amdgcn_cvt_f32_fp8(int((packed[e/4]>>(8*(e%4)))&255u),0);else v=in[base+k+gr*8+e];a[e]=ok?(_Float16)v:(_Float16)0.f;}'''
assert s.count(old)==2;s=s.replace(old,new);p.write_text(s)
p=W/'dectail-source/hip/deep_fast.hip';s=p.read_text();old=' for(uint it=0;it<2;it++){const uint seg=it*32+lane';assert s.count(old)==1;s=s.replace(old,' _Pragma("clang loop unroll(disable)")\n'+old);p.write_text(s)
for role,f in [('upwide','hip/wave_owned_mh.inc'),('dectail','hip/deep_fast.hip')]:
 (O/(role+'.patch')).write_text(subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(W/(role+'-source')/f)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for role,hip in [('deepbaseline',R/'hip'),('dectail',W/'dectail-source/hip')]:
 row=next(x for x in m.recipe(hip) if x[0]=='deep_fast-packed-fast');(W/(role+'.hip')).write_text(row[2])
