from pathlib import Path
import shutil,subprocess
R=Path.cwd();O=R/'Development/results/backfeed-20261028/knife3'
for role in ['baseline','candidate']:
 s=O/(role+'-source');shutil.copytree(R/'hip',s/'hip',dirs_exist_ok=True);(s/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',s/'Development/HIP/swin_persistent_types.h')
p=O/'candidate-source/hip/build-modules.ps1';s=p.read_text()
for name in ['c64-wave2','c64-wave2-fast']:
 old="@{ name = '"+name+"'; defines = @(";assert s.count(old)==1;s=s.replace(old,old+"'W2_FFN_W16_SMALL 3',")
p.write_text(s)
(O/'candidate.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/build-modules.ps1','--label','b/hip/build-modules.ps1',str(R/'hip/build-modules.ps1'),str(p)],capture_output=True,text=True).stdout)
