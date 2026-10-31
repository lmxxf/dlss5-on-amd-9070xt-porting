from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';old=r+'\\c512-prob\\modules';new=r+'\\c512-prob-den\\modules'
x=json.loads((p/'c512-prob-gold.json').read_text());x['name']='c512-prob-den-gold'
for f in x['files']:
 if old in f['path']:
  f['path']=f['path'].replace(old,new)
  if f['path'].endswith('c512-m32-mh.hsaco'):f['sha256']=hashlib.sha256((p/'c512-prob-den-compile/c512-m32-mh.hsaco').read_bytes()).hexdigest()
for j in x['jobs']:j['label']=j['label'].replace('c512-prob-gold/','c512-prob-den-gold/');j['arguments']=j['arguments'].replace(old,new)
(p/'c512-prob-den-gold.json').write_text(json.dumps(x,indent=2)+'\n')
y=copy.deepcopy(x);y['name']='c512-prob-den-perf';y['jobs']=[]
source=json.loads((p/'c512-prob-gold.json').read_text());y['files']+= [f for f in source['files']if old in f['path']]
for rnd in [1,2,3]:
 for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
  j=copy.deepcopy(next(j for j in x['jobs']if j['label']=='c512-prob-den-gold/small1080-'+arm));j['label']=f'c512-prob-den-perf/perf-small1080-r{rnd}-{slot}-{arm}'
  if arm=='baseline':j['arguments']=j['arguments'].replace(r+'\\baseline\\modules',old)
  a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1;y['jobs'].append(j)
(p/'c512-prob-den-perf.json').write_text(json.dumps(y,indent=2)+'\n')
(p/'c512-prob-den-setup.ps1').write_text("$ErrorActionPreference='Stop'\nif(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}\n"+f'New-Item -ItemType Directory -Force "{new}"|Out-Null\nCopy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"\nCopy-Item "{r}\\c512-prob-den-compile\\c512-m32-mh.hsaco" "{new}\\c512-m32-mh.hsaco"\n')
