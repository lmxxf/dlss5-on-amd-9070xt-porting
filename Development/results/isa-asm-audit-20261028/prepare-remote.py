from pathlib import Path
import json,hashlib
O=Path(__file__).parent;remote=r'D:\DLSSNR-Lab\isa-asm-audit-20261028'
for name in ['run-jobs.ps1','collect.ps1']:(O/name).write_text((O.parent/'backfeed-20261028'/name).read_text().replace('backfeed-20261028','isa-asm-audit-20261028'))
files=[{'path':r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe','sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for role in ['deepbaseline','dectail']:
 f=O/'work'/(role+'.hip');files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'compile/'+role,'exe':files[0]['path'],'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\deep_fast-packed-fast.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(O/'compile.json').write_text(json.dumps({'name':'asm-audit-decoder-compile','files':files,'jobs':jobs},indent=2))
