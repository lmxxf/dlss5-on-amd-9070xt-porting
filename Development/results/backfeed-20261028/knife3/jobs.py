from pathlib import Path
import json,copy,hashlib
P=Path(__file__).parent;root=P.parent;remote=r'D:\DLSSNR-Lab\backfeed-20261028';d=json.loads((root/'final-default.json').read_text());jobs=[]
for j in d['jobs']:
 profile=j['label'].split('/final-')[1].removesuffix('-candidate')
 for arm in ['baseline','candidate']:
  x=copy.deepcopy(j);x['label']=f'knife3/gold-{profile}-{arm}';x['env']={};x.pop('compare_to',None)
  if arm=='candidate':x['arguments']=x['arguments'].replace(r'gap-phase6-20261023\knife1\modules',r'backfeed-20261028\knife3\candidate\modules');x['compare_to']=remote+f'\\knife3\\gold-{profile}-baseline\\out-first.f16'
  jobs.append(x)
files=list(d['files'])
for f in d['files']:
 if 'gap-phase6-20261023\\knife1\\modules\\' in f['path']:
  x=copy.deepcopy(f);x['path']=x['path'].replace(r'gap-phase6-20261023\knife1\modules',r'backfeed-20261028\knife3\candidate\modules');name=x['path'].split('\\')[-1]
  if name in ['c64-wave2.hsaco','c64-wave2-fast.hsaco']:x['sha256']=hashlib.sha256((P/'candidate/gfx1201'/name).read_bytes()).hexdigest()
  files.append(x)
out={'name':'backfeed-smallw16-gold','files':files,'jobs':jobs};(P/'gold.json').write_text(json.dumps(out,indent=2));perf=[]
for profile in ['small900','small1080','original960','original1088']:
 for rnd in range(1,4):
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   x=copy.deepcopy(next(y for y in jobs if y['label']==f'knife3/gold-{profile}-{arm}'));x['label']=f'knife3/perf-{profile}-r{rnd}-{slot}-{arm}';x['arguments']=x['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');x['frame_count']=240;x['edges_only']=1;perf.append(x)
out.update(name='backfeed-smallw16-perf',jobs=perf);(P/'perf.json').write_text(json.dumps(out,indent=2))
