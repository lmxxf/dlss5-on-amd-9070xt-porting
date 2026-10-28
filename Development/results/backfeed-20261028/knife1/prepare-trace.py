from pathlib import Path
import json,hashlib,copy
P=Path(__file__).parent;R=P.parents[3];remote=r'D:\DLSSNR-Lab\backfeed-20261028'
old=json.loads((R/'Development/results/gap-phase4-20261023/knife2/trace-jobs.json').read_text());d=json.loads((P.parent/'gold.json').read_text());jobs=[]
for j in old['jobs']:
 if 'smallfree1088' in j['label']:continue
 x=copy.deepcopy(j);arm='candidate' if 'candidate' in j['label'] else 'baseline';x['exe']=remote+r'\trace.exe';x['env']={'NR_BACKFEED_SMALL_C256':str(int(arm=='candidate')),'NR_BACKFEED_VIT400':str(int(arm=='candidate'))};x['arguments']=x['arguments'].replace(r'gap-phase4-20261023\knife1\modules',r'gap-phase6-20261023\knife1\modules').replace(r'gap-phase4-20261023\knife2\trace-',r'backfeed-20261028\knife1\trace-');jobs.append(x)
d['jobs']=jobs;d['name']='backfeed-trace'
d['files'] += [f for f in old['files'] if f['path'].endswith('.rgba32f')]
d['files'].append({'path':remote+r'\trace.exe','sha256':hashlib.sha256((P/'trace.exe').read_bytes()).hexdigest()})
(P.parent/'trace.json').write_text(json.dumps(d,indent=2))
