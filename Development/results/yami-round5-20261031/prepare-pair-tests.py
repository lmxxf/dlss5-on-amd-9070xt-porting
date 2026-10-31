from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';x=json.loads((p/'vit-fix-gold.json').read_text());old=r+'\\vit-fix\\modules';new=r+'\\pair-scope\\modules'
for f in x['files']:
 if old in f['path']:
  f['path']=f['path'].replace(old,new)
  if f['path'].endswith('deep_fast-packed-fast.hsaco'):f['sha256']=hashlib.sha256((p/'pair-final-compile/deep_fast-packed-fast.hsaco').read_bytes()).hexdigest()
for name in ['pair-frame.exe','pair-window.exe']:x['files'].append({'path':r+'\\'+name,'sha256':hashlib.sha256((p/name).read_bytes()).hexdigest()})
for j in x['jobs']:
 j['label']=j['label'].replace('vit-fix-gold','pair-gold');j['arguments']=j['arguments'].replace(old,new);j['arm']='candidate' if j['label'].endswith('-candidate') else 'baseline'
 if j['arm']=='candidate':j['exe']=r+'\\'+('pair-window.exe' if 'smallwindow' in j['label'] else 'pair-frame.exe')
x['name']='qualified-small1080-pair-gold';(p/'pair-gold.json').write_text(json.dumps(x,indent=2));jobs=[]
for profile in ['small1080']:
 for rnd in [1,2,3]:
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   j=copy.deepcopy(next(j for j in x['jobs'] if j['label']=='pair-gold/'+profile+'-'+arm));j['label']=f'pair-perf/perf-{profile}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1;jobs.append(j)
y=copy.deepcopy(x);y['name']='qualified-small1080-product';y['jobs']=jobs;(p/'pair-perf.json').write_text(json.dumps(y,indent=2))
s=f'''$ErrorActionPreference='Stop'
$r='{r}'
New-Item -ItemType Directory -Force "$r\\pair-scope\\modules"|Out-Null
Copy-Item 'D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*' "$r\\pair-scope\\modules"
Copy-Item "$r\\pair-final-compile\\deep_fast-packed-fast.hsaco" "$r\\pair-scope\\modules\\deep_fast-packed-fast.hsaco"
''';(p/'pair-setup.ps1').write_text(s)
