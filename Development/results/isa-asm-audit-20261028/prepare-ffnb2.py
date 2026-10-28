from pathlib import Path
import shutil,subprocess
R=Path.cwd();P=R/'Development/results/isa-asm-audit-20261028';S=P/'work/ffnb2-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/wave_owned_mh.inc';s=p.read_text();old=' constexpr uint B=W2_FFN_QT_BATCH;';assert s.count(old)==1;s=s.replace(old,' constexpr uint B=C==64?W2_FFN_QT_BATCH:2;');p.write_text(s)
(P/'ffnb2.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/wave_owned_mh.inc','--label','b/hip/wave_owned_mh.inc',str(R/'hip/wave_owned_mh.inc'),str(p)],capture_output=True,text=True).stdout)
