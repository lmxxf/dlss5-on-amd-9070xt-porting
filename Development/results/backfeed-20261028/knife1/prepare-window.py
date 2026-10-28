from pathlib import Path
import json,copy,hashlib,struct
P=Path(__file__).parent;R=P.parent;remote=r'D:\DLSSNR-Lab\backfeed-20261028'
a=bytearray()
for y in range(480):
 for x in range(854):a+=struct.pack('<4H',*(0x2000+(x*17+y*31+c*53)%10240 for c in range(3)),0x3c00)
(P/'window.f16').write_bytes(a)
d=json.loads((R/'gold.json').read_text());j=copy.deepcopy(next(j for j in d['jobs'] if j['label']=='knife1/gold-small720-baseline'));jobs=[]
for arm in ['baseline','candidate']:
 x=copy.deepcopy(j);x['label']='knife1/gold-smallwindow-'+arm;x['exe']=remote+r'\frame-smallwindow.exe';x['flags']+='\nDLSS5_NETWORK_FREE_RES=1\n';x['env']={'NR_BACKFEED_SMALL_C256':str(int(arm=='candidate')),'NR_BACKFEED_VIT400':str(int(arm=='candidate'))};x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\combined-vs041-20261006\input.f16',remote+r'\window.f16');x.pop('gold',None)
 if arm=='candidate':x['compare_to']=remote+r'\knife1\gold-smallwindow-baseline\out-first.f16'
 jobs.append(x)
d['jobs']=jobs;d['name']='backfeed-window-gold'
for f in ['frame-smallwindow.exe','window.f16']:d['files'].append({'path':remote+'\\'+f,'sha256':hashlib.sha256((P/f).read_bytes()).hexdigest()})
(R/'gold-window.json').write_text(json.dumps(d,indent=2))
