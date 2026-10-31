from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';old=r+'\\c512-prob\\modules';new=r+'\\c512-scope\\modules'
x=json.loads((p/'c512-prob-gold.json').read_text());x['name']='c512-scope-gold'
for f in x['files']:
 if old in f['path']:
  f['path']=f['path'].replace(old,new)
  if f['path'].endswith('c512-m32-mh.hsaco'):f['sha256']=hashlib.sha256((p/'c512-scope-compile/c512-m32-mh.hsaco').read_bytes()).hexdigest()
for name in ['c512-scope-standard.exe','c512-scope-window.exe']:x['files'].append(dict(path=r+'\\'+name,sha256=hashlib.sha256((p/name).read_bytes()).hexdigest()))
for j in x['jobs']:
 j['label']=j['label'].replace('c512-prob-gold','c512-scope-gold');j['arguments']=j['arguments'].replace(old,new)
 if j['label'].endswith('-candidate'):
  j['exe']=r+'\\'+('c512-scope-window.exe'if 'smallwindow'in j['label']else'c512-scope-standard.exe')
  calls=32 if '/small1080-'in j['label']else 0
  j['route_regex']=r'R5_C512_PROB requested=(\d+) available=\d+ calls=(\d+)';j['route_requested']=1;j['route_calls']=calls
(p/'c512-scope-gold.json').write_text(json.dumps(x,indent=2)+'\n')
y=copy.deepcopy(x);y['name']='c512-scope-perf';y['jobs']=[]
for profile in ['original1088','original1152','original960','smallwindow','small720','small900','small1080']:
 for rnd in [1,2,3]:
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   j=copy.deepcopy(next(j for j in x['jobs']if j['label']=='c512-scope-gold/'+profile+'-'+arm));j['label']=f'c512-scope-perf/perf-{profile}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1
   if 'route_calls'in j:j['route_calls']=980 if profile=='small1080'else 0
   y['jobs'].append(j)
(p/'c512-scope-perf.json').write_text(json.dumps(y,indent=2)+'\n')
y=copy.deepcopy(x);y['name']='c512-scope-compat';y['jobs']=[]
for case in ['disabled','missing','fast0','ae1']:
 for arm in ['baseline','candidate']:
  j=copy.deepcopy(next(j for j in x['jobs']if j['label']=='c512-scope-gold/small1080-'+arm));j['label']='c512-scope-compat/'+case+'-'+arm
  j.pop('gold',None)
  if arm=='candidate':j['compare_to']=r+'\\c512-scope-compat\\'+case+'-baseline';j['route_calls']=0
  if case=='disabled':j['env']['NR_R5_C512_PROB']='0';j['route_requested']=0
  if case=='missing':j['arguments']=j['arguments'].replace(new,r+'\\baseline\\modules')
  if case=='fast0':j['flags']+='\nDLSS5_FAST_NUMERIC=0\n'
  if case=='ae1':j['flags']+='\nDLSS5_VIT_ADAPTIVE=1\n'
  y['jobs'].append(j)
(p/'c512-scope-compat.json').write_text(json.dumps(y,indent=2)+'\n')
(p/'c512-scope-setup.ps1').write_text("$ErrorActionPreference='Stop'\nif(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}\n"+f'New-Item -ItemType Directory -Force "{new}"|Out-Null\nCopy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"\nCopy-Item "{r}\\c512-scope-compile\\c512-m32-mh.hsaco" "{new}\\c512-m32-mh.hsaco"\n')
