from pathlib import Path
import json,copy,hashlib
P=Path(__file__).parent;root=P.parent;remote=r'D:\DLSSNR-Lab\backfeed-20261028'
d=json.loads((root/'final-default.json').read_text());jobs=[]
for j in d['jobs']:
 if 'smallwindow' in j['label']:continue
 profile=j['label'].split('/final-')[1].removesuffix('-candidate')
 for arm in ['baseline','candidate']:
  x=copy.deepcopy(j);x['label']=f'knife2/gold-{profile}-{arm}';x['env']={};x.pop('compare_to',None)
  if arm=='candidate':x['arguments']=x['arguments'].replace(r'gap-phase6-20261023\knife1\modules',r'backfeed-20261028\knife2\qkv\modules');x['compare_to']=remote+f'\\knife2\\gold-{profile}-baseline\\out-first.f16'
  jobs.append(x)
files=list(d['files'])
# Local modules may be in hip/modules: lock all candidate directory bytes from baseline manifest instead.
if len(files)==len(d['files']):
 for f in d['files']:
  if 'gap-phase6-20261023\\knife1\\modules\\' in f['path']:
   x=copy.deepcopy(f);x['path']=x['path'].replace(r'gap-phase6-20261023\knife1\modules',r'backfeed-20261028\knife2\qkv\modules')
   if x['path'].endswith('vit-stream-fast.hsaco'):x['sha256']=hashlib.sha256((P/'qkv.hsaco').read_bytes()).hexdigest()
   files.append(x)
out={'name':'backfeed-qkv-gold','files':files,'jobs':jobs};(P/'gold.json').write_text(json.dumps(out,indent=2));perf=[]
for j in jobs:
 if not j['label'].endswith('-baseline'):continue
 profile=j['label'].split('/gold-')[1].removesuffix('-baseline')
 for rnd in range(1,4):
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   x=copy.deepcopy(next(y for y in jobs if y['label']==f'knife2/gold-{profile}-{arm}'));x['label']=f'knife2/perf-{profile}-r{rnd}-{slot}-{arm}';x['arguments']=x['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');x['frame_count']=240;x['edges_only']=1;perf.append(x)
out.update(name='backfeed-qkv-perf',jobs=perf);(P/'perf.json').write_text(json.dumps(out,indent=2))
