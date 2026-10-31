from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';x=json.loads((p/'c512-scope-verified-gold.json').read_text());x['name']='c512-slot-gold'
for n in ['c512-slot-standard.exe','c512-slot-window.exe']:x['files'].append(dict(path=r+'\\'+n,sha256=hashlib.sha256((p/n).read_bytes()).hexdigest()))
for j in x['jobs']:
 j['label']=j['label'].replace('c512-scope-verified-gold/','c512-slot-gold/')
 if j['label'].endswith('-candidate'):j['exe']=j['exe'].replace('c512-scope-standard.exe','c512-slot-standard.exe').replace('c512-scope-window.exe','c512-slot-window.exe')
(p/'c512-slot-gold.json').write_text(json.dumps(x,indent=2)+'\n')
y=copy.deepcopy(x);y['name']='c512-slot-perf';y['jobs']=[]
for rnd in [1,2,3]:
 for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
  j=copy.deepcopy(next(j for j in x['jobs']if j['label']=='c512-slot-gold/small1080-'+arm));j['label']=f'c512-slot-perf/perf-small1080-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1
  if 'route_calls'in j:j['route_calls']=964
  y['jobs'].append(j)
(p/'c512-slot-perf.json').write_text(json.dumps(y,indent=2)+'\n')
