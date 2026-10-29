from pathlib import Path
import shutil,json,hashlib,subprocess
p=Path(__file__).parent;q=p.parent/'c32-stage-phase5-20261029'
for n in ['run-jobs.ps1','collect.ps1','collect-local.py','cleanup-case.ps1','analyze.py','analyze-c32.py','inspect-isa.py','index-evidence.py','make-case.py','run-case.sh','prepare-profile-device.py','prepare-profile-single-initial.py','pure.cpp','prepare3.py','prepare3-jobs.py','build-final.sh','prepare-final.py','fetch-full.ps1','prepare-final-tests.py','analyze-final-nmmn.py','prepare-locks.ps1','verify-locks.ps1','verify-protected.py']:
 (p/n).write_text((q/n).read_text().replace('c32-stage-phase5-20261029','c32-stage-phase5b-20261029'))
for n in ['.gitignore','.gitattributes','upstream-source-lock.json']:shutil.copyfile(q/n,p/n)
(p/'work').mkdir();(p/'payload').mkdir();(p/'start.json').write_text(json.dumps({'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'modules':{n:h for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines())}},indent=2))
# Reconstruct the isolated fusion, including the explicit rounding boundary of the separate MH module.
f=p/'prepare3.py';s=f.read_text();s=s.replace('float v=Hrtz(0.f+sum[e]);','float v=float(__builtin_amdgcn_cvt_pkrtz(0.f+sum[e],0.f)[0]);').replace('float fv=__builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(cv,0.f,0,false),0);poolout','float fv=__builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(cv,0.f,0,false),0);fv=fv+0.f;poolout');f.write_text(s)
