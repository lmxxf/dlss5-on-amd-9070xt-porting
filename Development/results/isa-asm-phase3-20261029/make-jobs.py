from pathlib import Path
import json,copy,hashlib,sys,ntpath
P=Path(__file__).parent;R=P.parents[2];remote=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029';base=json.loads((R/'Development/results/backfeed-20261028/final-default.json').read_text());config=json.loads(Path(sys.argv[1]).read_text());sums={n:h for h,n in (l.split() for l in (R/'hip/SHA256SUMS').read_text().splitlines())}
for role,replacements in config.items():
 files=[f for f in base['files'] if not f['path'].endswith('.hsaco')];jobs=[]
 for n,h in sums.items():
  if not n.startswith('gfx1201/'):continue
  name=n.split('/')[1];files.append({'path':ntpath.join(remote,'baseline','modules',name),'sha256':h});local=replacements.get(name);files.append({'path':ntpath.join(remote,role,'modules',name),'sha256':hashlib.sha256(Path(local).read_bytes()).hexdigest() if local else h})
 for j in base['jobs']:
  profile=j['label'].split('/final-')[1].removesuffix('-candidate')
  for arm in ['baseline','candidate']:
   x=copy.deepcopy(j);x['label']=f'{role}/gold-{profile}-{arm}';x['env']={};x.pop('compare_to',None);mod=ntpath.join(remote,'baseline' if arm=='baseline'else role,'modules');x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules',mod)
   if arm=='candidate':x['compare_to']=ntpath.join(remote,role,'gold-'+profile+'-baseline','out-first.f16')
   jobs.append(x)
 out={'name':role+'-gold','files':files,'jobs':jobs};(P/(role+'-gold.json')).write_text(json.dumps(out,indent=2));perf=[]
 for profile in ['small900','small1080','original960','original1088']:
  for rnd in range(1,4):
   for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
    x=copy.deepcopy(next(j for j in jobs if j['label']==f'{role}/gold-{profile}-{arm}'));x['label']=f'{role}/perf-{profile}-r{rnd}-{slot}-{arm}';x['arguments']=x['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');x['frame_count']=240;x['edges_only']=1;x['compare_to']=ntpath.join(remote,role,'gold-'+profile+'-baseline','out-first.f16');perf.append(x)
 out.update(name=role+'-perf',jobs=perf);(P/(role+'-perf.json')).write_text(json.dumps(out,indent=2))
