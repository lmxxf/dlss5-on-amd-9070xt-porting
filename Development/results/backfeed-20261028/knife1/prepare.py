from pathlib import Path
import json,hashlib,copy
R=Path(__file__).resolve().parents[4]; O=Path(__file__).parent; root=O.parent
remote=r'D:\DLSSNR-Lab\backfeed-20261028'
d=json.loads((R/'Development/results/gap-phase6-20261023/knife1/limited-default-jobs.json').read_text())
jobs=[]
for j in d['jobs']:
 if 'limited-default-' not in j['label'] or 'free' in j['profile']:continue
 for arm in ['baseline','candidate']:
  x=copy.deepcopy(j);x['label']='knife1/gold-'+j['profile']+'-'+arm;x['exe']=remote+r'\frame-standard.exe';x['env']={'NR_BACKFEED_SMALL_C256':str(int(arm=='candidate')),'NR_BACKFEED_VIT400':str(int(arm=='candidate'))};x.pop('required_stderr',None)
  if arm=='candidate':x['compare_to']=remote+'\\knife1\\gold-'+j['profile']+'-baseline\\out-first.f16'
  jobs.append(x)
files=[f for f in d['files'] if not f['path'].endswith('.exe')]
files.append({'path':remote+r'\frame-standard.exe','sha256':hashlib.sha256((O/'frame-standard.exe').read_bytes()).hexdigest()})
(root/'gold.json').write_text(json.dumps({'name':'backfeed-knife1-gold','files':files,'jobs':jobs},indent=2))
s=(R/'Development/results/gap-phase6-20261023/run-jobs.ps1').read_text().replace('gap-phase6-20261023','backfeed-20261028')
(root/'run-jobs.ps1').write_text(s)
print('gold jobs',len(jobs))
