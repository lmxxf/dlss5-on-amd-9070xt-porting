import json,hashlib,sys
from pathlib import Path
W=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023/knife1');O=Path(__file__).parent
remote=r'D:\DLSSNR-Lab\gap-phase5-20261023'
files=[dict(path=remote+'\\rtc_compile-current.exe',sha256='553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197')];jobs=[]
for role in sys.argv[1:] or ['baseline','pair','perm','combined']:
 f=W/(role+'.generated.hip');files.append(dict(path=remote+'\\knife1\\'+f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
 jobs.append(dict(label='knife1/compile-'+role,exe=remote+'\\rtc_compile-current.exe',env={'RTC_EXTRA_OPTS':''},arguments='"{OUT}\\c512-m32-mh.hsaco" "'+remote+'\\knife1\\'+f.name+'" comgr gfx1201'))
(O/'compile-jobs.json').write_text(json.dumps(dict(name='phase5-c512-comgr',files=files,jobs=jobs),indent=2)+'\n')
