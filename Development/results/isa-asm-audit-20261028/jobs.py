from pathlib import Path
import json,copy,hashlib
P=Path(__file__).parent;R=P.parents[2];remote=r'D:\DLSSNR-Lab\isa-asm-audit-20261028';base=json.loads((P.parent/'backfeed-20261028/final-default.json').read_text())
for role,module,local in [('upwide','c64-wave2-fast',P/'work/upwide-final/gfx1201/c64-wave2-fast.hsaco'),('dectail','deep_fast-packed-fast',P/'work/dectail.hsaco'),('ffnb2','c64-wave2-fast',P/'work/ffnb2/gfx1201/c64-wave2-fast.hsaco')]:
 files=list(base['files']);jobs=[]
 for f in base['files']:
  if 'gap-phase6-20261023\\knife1\\modules\\' in f['path']:
   x=copy.deepcopy(f);x['path']=x['path'].replace(r'gap-phase6-20261023\knife1\modules','isa-asm-audit-20261028'+chr(92)+role+chr(92)+'modules')
   if x['path'].endswith(module+'.hsaco'):x['sha256']=hashlib.sha256(local.read_bytes()).hexdigest()
   files.append(x)
 for j in base['jobs']:
  profile=j['label'].split('/final-')[1].removesuffix('-candidate')
  for arm in ['baseline','candidate']:
   x=copy.deepcopy(j);x['label']=f'{role}/gold-{profile}-{arm}';x['env']={};x.pop('compare_to',None)
   if arm=='candidate':x['arguments']=x['arguments'].replace(r'gap-phase6-20261023\knife1\modules','isa-asm-audit-20261028'+chr(92)+role+chr(92)+'modules');x['compare_to']=remote+f'\\{role}\\gold-{profile}-baseline\\out-first.f16'
   jobs.append(x)
 out={'name':'asm-'+role+'-gold','files':files,'jobs':jobs};(P/(role+'-gold.json')).write_text(json.dumps(out,indent=2));perf=[]
 for profile in ['small900','small1080','original960','original1088']:
  for rnd in range(1,4):
   for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
    x=copy.deepcopy(next(j for j in jobs if j['label']==f'{role}/gold-{profile}-{arm}'));x['label']=f'{role}/perf-{profile}-r{rnd}-{slot}-{arm}';x['arguments']=x['arguments'].replace(' 3 0 ',' 240 0 ').replace(' 0 0 0 0',' 0 1 0 0');x['frame_count']=240;x['edges_only']=1;perf.append(x)
 out.update(name='asm-'+role+'-perf',jobs=perf);(P/(role+'-perf.json')).write_text(json.dumps(out,indent=2))
