from pathlib import Path
import copy,json,hashlib,re
p=Path(__file__).parent;r=r'D:\DLSSNR-Lab\yami-round5-20261031';src=json.loads((p/'r2d-gold.json').read_text());files=[f for f in src['files'] if '\\candidate\\modules\\' not in f['path']];files.append({'path':r+'\\sp128-frame.exe','sha256':hashlib.sha256((p/'sp128-frame.exe').read_bytes()).hexdigest()});jobs=[]
for j in src['jobs']:
 if not any('/'+q+'-' in j['label'] for q in ['original1088','original1152']):continue
 x=copy.deepcopy(j);x['label']=x['label'].replace('r2d-gold','sp128-gold');x['arguments']=x['arguments'].replace('\\candidate\\modules','\\baseline\\modules')
 if x['label'].endswith('-candidate'):x['exe']=r+'\\sp128-frame.exe';x['env']={'NR_R5_SP128':'1'}
 jobs.append(x)
(p/'sp128-gold.json').write_text(json.dumps({'name':'sp128-fast-middle-stages','files':files,'jobs':jobs},indent=2));perf=[]
for profile in ['original1088','original1152']:
 for rnd in [1,2,3]:
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   x=copy.deepcopy(next(j for j in jobs if j['label']=='sp128-gold/'+profile+'-'+arm));x['label']=f'sp128-perf/perf-{profile}-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',x['arguments']);a[4]='240';a[8]='1';x['arguments']=' '.join(a);x['frame_count']=240;x['edges_only']=1;perf.append(x)
(p/'sp128-perf.json').write_text(json.dumps({'name':'sp128-product-before-after','files':files,'jobs':perf},indent=2))
