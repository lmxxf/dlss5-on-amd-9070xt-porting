from pathlib import Path
import subprocess
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';S=P/'work/knife2-source';p=S/'hip/wave_owned_mh.inc';s=p.read_text();old='W2_Q8_END(packed);hidden[tile][qi]=packed;';assert s.count(old)==1;s=s.replace(old,old+'__builtin_amdgcn_sched_barrier(0);');p.write_text(s);(P/'knife2-scheduled.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/wave_owned_mh.inc','--label','b/hip/wave_owned_mh.inc',str(R/'hip/wave_owned_mh.inc'),str(p)],capture_output=True,text=True).stdout)
