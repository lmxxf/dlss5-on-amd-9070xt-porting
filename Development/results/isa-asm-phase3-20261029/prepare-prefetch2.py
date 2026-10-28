from pathlib import Path
import shutil,subprocess
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';S=P/'work/knife2-pref-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/wave_owned_mh.inc';s=p.read_text();s='#ifndef W2_CONTRACT_PREFETCH\n#define W2_CONTRACT_PREFETCH 0\n#endif\n'+s;needle='''   _Pragma("unroll") for(uint tile=0;tile<T;tile++){
    i2 hidden[B];''';assert s.count(needle)==1;s=s.replace(needle,'''   _Pragma("unroll") for(uint tile=0;tile<T;tile++){
    i2 cb[2];if constexpr(W2_CONTRACT_PREFETCH){__builtin_amdgcn_sched_barrier(0);_Pragma("unroll") for(uint ci=0;ci<2;ci++)cb[ci]=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);__builtin_amdgcn_sched_barrier(0);}
    i2 hidden[B];''');old='''     i2 b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);''';assert s.count(old)>=1;s=s.replace(old,'''     i2 b;if constexpr(W2_CONTRACT_PREFETCH)b=cb[ci];else b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);''',1);p.write_text(s);(P/'knife2-prefetch-only.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/wave_owned_mh.inc','--label','b/hip/wave_owned_mh.inc',str(R/'hip/wave_owned_mh.inc'),str(p)],capture_output=True,text=True).stdout)
p=S/'hip/build-modules.ps1';s=p.read_text();needle="@{ name = 'c64-wave2-fast'; defines = @(";s=s.replace(needle,needle+"'W2_CONTRACT_PREFETCH 1',");p.write_text(s)
