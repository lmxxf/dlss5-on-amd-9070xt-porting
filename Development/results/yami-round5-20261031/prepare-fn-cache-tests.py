from pathlib import Path
import json,hashlib,copy,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';x=json.loads((p/'r2d-gold.json').read_text());x['files']=[f for f in x['files']if '\\candidate\\modules\\' not in f['path']]
for f in ['fn-cache-frame.exe','fn-cache-window.exe']:x['files'].append({'path':r+'\\'+f,'sha256':hashlib.sha256((p/f).read_bytes()).hexdigest()})
for j in x['jobs']:
 j['label']=j['label'].replace('r2d-gold','fn-cache-gold');j['arguments']=j['arguments'].replace('\\candidate\\modules','\\baseline\\modules');j['arm']='candidate'if j['label'].endswith('-candidate')else'baseline'
 if j['arm']=='candidate':j['exe']=r+'\\'+('fn-cache-window.exe' if 'smallwindow' in j['label'] else 'fn-cache-frame.exe');j['required_stderr']=['DIRECT_FN_CACHE hits=[1-9]']
x['name']='direct-function-cache-gold';(p/'fn-cache-gold.json').write_text(json.dumps(x,indent=2));jobs=[]
for prof in ['original1088','original960','small1080','small900']:
 for rnd in [1,2,3]:
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   j=copy.deepcopy(next(j for j in x['jobs']if j['label']=='fn-cache-gold/'+prof+'-'+arm));j['label']=f'fn-cache-perf/perf-{prof}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='240';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=240;j['edges_only']=1;jobs.append(j)
x['jobs']=jobs;x['name']='direct-function-cache-product';(p/'fn-cache-perf.json').write_text(json.dumps(x,indent=2))
