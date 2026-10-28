from pathlib import Path
import json,copy,hashlib
P=Path(__file__).parent;R=P.parents[2];remote=r'D:\DLSSNR-Lab\isa-asm-phase2-20261028';d=json.loads((P/'knife1-final-gold.json').read_text())
for f in d['files']:
 if f['path'].endswith('frame-scoped-standard.exe'):f['sha256']=hashlib.sha256((P/'work/frame-scoped-standard.exe').read_bytes()).hexdigest()
 if f['path'].endswith('frame-scoped-window.exe'):f['sha256']=hashlib.sha256((P/'work/frame-scoped-window.exe').read_bytes()).hexdigest()
for j in d['jobs']:
 j['label']=j['label'].replace('final-gold-','cached-gold-')
 if j.get('compare_to'):j['compare_to']=j['compare_to'].replace('final-gold-','cached-gold-')
j=copy.deepcopy(next(j for j in d['jobs'] if j['label']=='knife1/cached-gold-original1088-candidate'));j['label']='knife1/cached-default-original1088';j['env']={};d['jobs'].append(j);d['name']='phase2-cached-vfrag-gold';(P/'knife1-cached-gold.json').write_text(json.dumps(d,indent=2))
p=json.loads((P/'knife1-final-perf.json').read_text());p['files']=d['files']+[f for f in json.loads((P.parent/'backfeed-20261028/final-default.json').read_text())['files'] if f['path'].endswith(('.exe','.hsaco'))]
for j in p['jobs']:
 j['label']=j['label'].replace('final-perf-','product-perf-');j['compare_to']=remote+r'\knife1\cached-gold-original1088-baseline\out-first.f16'
 if j['label'].endswith('-baseline'):
  j['exe']=r'D:\DLSSNR-Lab\backfeed-20261028\frame-standard.exe';j['arguments']=j['arguments'].replace(remote+r'\knife1\scoped-modules',r'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules');j['required_stderr']=[r'model requested=1x effective=1x blocks=71']
p['name']='phase2-product-before-after-vfrag';(P/'knife1-product-perf.json').write_text(json.dumps(p,indent=2))
