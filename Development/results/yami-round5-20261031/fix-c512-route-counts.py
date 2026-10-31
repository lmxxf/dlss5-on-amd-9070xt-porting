from pathlib import Path
import json,copy,re
p=Path(__file__).parent;x=json.loads((p/'c512-scope-gold.json').read_text());x['name']='c512-scope-verified-gold'
for j in x['jobs']:
 j['label']=j['label'].replace('c512-scope-gold/','c512-scope-verified-gold/')
 if j.get('route_calls'):j['route_calls']=16
(p/'c512-scope-verified-gold.json').write_text(json.dumps(x,indent=2)+'\n')
y=json.loads((p/'c512-scope-perf.json').read_text())
for j in y['jobs']:
 if j.get('route_calls'):j['route_calls']=964
(p/'c512-scope-perf.json').write_text(json.dumps(y,indent=2)+'\n')
z=copy.deepcopy(x);z['name']='c512-scope-route-counts';z['jobs']=[]
for n in [1,4]:
 j=copy.deepcopy(next(j for j in x['jobs']if j['label'].endswith('/small1080-candidate')));j['label']=f'c512-scope-route-counts/n{n}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]=str(n);j['arguments']=' '.join(a);j['frame_count']=n;j['route_calls']=4*(n+1);z['jobs'].append(j)
(p/'c512-scope-route-counts.json').write_text(json.dumps(z,indent=2)+'\n')
