import json,copy,hashlib
from pathlib import Path
R=Path(__file__).parent;P=R.parent/'gap-phase4-20261023';W=Path('/home/lmxxf/work/dlss5-gap-phase5-20261023');remote=r'D:\DLSSNR-Lab\gap-phase5-20261023'
def save(n,jobs):
 files=[]
 for f in (W/'baseline/modules').glob('*.hsaco'):
  files.append(dict(path=remote+'\\baseline\\modules\\'+f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
 d=dict(name=n,files=files,jobs=jobs);(R/'knife1'/(n+'.json')).write_text(json.dumps(d,indent=2)+'\n')
jobs=[]
source=json.loads((P/'knife1/gold-jobs.json').read_text())
for variant in ['baseline','pair','perm','combined']:
 for old in source['jobs']:
  if not old['label'].endswith('baseline'):continue
  j=copy.deepcopy(old);j['label']='knife1/'+old['label'].split('/')[-1].replace('baseline',variant)
  j['exe']=remote+'\\baseline\\frame.exe';j['env']={'SMALL_FRAME_CAPTURE':'1'}
  # Source manifest uses the previous experimental host/modules. Replace module argument explicitly.
  import re
  j['arguments']=re.sub(r'"[^" ]*modules"',lambda _: '"'+remote+'\\'+('baseline' if variant=='baseline' else 'knife1/'+variant).replace('/','\\')+'\\modules"',j['arguments'])
  for k in ['route_regex','route_requested','route_calls','required_stderr']:j.pop(k,None)
  j.pop('compare_to',None)
  jobs.append(j)
save('gold-jobs',jobs)

# Performance candidate selection happens only after formal ISA and Gold gates.
def make_perf(variant):
 jobs=[]
 for old in json.loads((P/'knife1/perf-jobs.json').read_text())['jobs']:
  j=copy.deepcopy(old);role=j['label'].split('-')[-1]
  j['exe']=remote+'\\baseline\\frame.exe';j['env']={}
  mod='baseline' if role=='baseline' else 'knife1/'+variant
  j['arguments']=re.sub(r'"[^" ]*modules"',lambda _: '"'+remote+'\\'+mod.replace('/','\\')+'\\modules"',j['arguments'])
  for k in ['route_regex','route_requested','route_calls','required_stderr']:j.pop(k,None)
  j.pop('compare_to',None);j['label']='knife1/perf-'+variant+'/'+j['label'].split('/')[-1];jobs.append(j)
 save('perf-'+variant+'-jobs',jobs)
if __name__=='__main__':
 import sys
 if len(sys.argv)>1:make_perf(sys.argv[1])
