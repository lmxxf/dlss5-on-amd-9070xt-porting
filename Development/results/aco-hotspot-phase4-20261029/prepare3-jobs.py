from pathlib import Path
import json,copy,hashlib,ntpath,shutil
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\aco-hotspot-phase4-20261029';base=json.loads(Path('Development/results/backfeed-20261028/final-default.json').read_text());sums={n:h for h,n in(l.split()for l in Path('hip/SHA256SUMS').read_text().splitlines())};replacements={'3a':p/'work/3a-build/gfx1201/c64-wave2-fast.hsaco','3b':p/'work/3b/isolated.hsaco'};configs={}
for role,candidate in replacements.items():
 shutil.copyfile(candidate,p/'payload'/(role+'-c64-wave2-fast.hsaco'));files=[f for f in base['files']if not f['path'].endswith('.hsaco')];jobs=[]
 for n,h in sums.items():
  if not n.startswith('gfx1201/'):continue
  name=n.split('/')[1]
  for arm in ['baseline',role]:files.append({'path':ntpath.join(r,arm,'modules',name),'sha256':hashlib.sha256(candidate.read_bytes()).hexdigest()if arm==role and name=='c64-wave2-fast.hsaco'else h})
 for j in base['jobs']:
  profile=j['label'].split('/final-')[1].removesuffix('-candidate')
  for arm in ['baseline','candidate']:
   x=copy.deepcopy(j);x['label']=f'{role}/gold-{profile}-{arm}';x['env']={};x.pop('compare_to',None);mod=ntpath.join(r,'baseline'if arm=='baseline'else role,'modules');x['arguments']=x['arguments'].replace(r'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules',mod)
   if arm=='candidate':x['compare_to']=ntpath.join(r,role,f'gold-{profile}-baseline','out-first.f16')
   jobs.append(x)
 (p/(role+'-gold.json')).write_text(json.dumps({'name':role+'-seven-gold','files':files,'jobs':jobs},indent=2));perf=[]
 for profile in ['small900','small1080','original960','original1088']:
  for rnd in range(1,4):
   for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
    x=copy.deepcopy(next(j for j in jobs if j['label']==f'{role}/gold-{profile}-{arm}'));x['label']=f'{role}/perf-{profile}-r{rnd}-{slot}-{arm}';x['arguments']=x['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');x['frame_count']=240;x['edges_only']=1;perf.append(x)
 (p/(role+'-perf.json')).write_text(json.dumps({'name':role+'-four-profile-ABBA','files':files,'jobs':perf},indent=2));configs[role]={'sha256':hashlib.sha256(candidate.read_bytes()).hexdigest()}
(p/'3-candidates.json').write_text(json.dumps(configs,indent=2))
