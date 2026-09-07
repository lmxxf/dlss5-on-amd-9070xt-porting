"""Audit profiling run correctness and rank intervals (including queue gaps)."""
from pathlib import Path
import json,re,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('release/native-network70-profile'));args=p.parse_args()
root=args.root;log=(root/'network.stdout.log').read_text()
rows=[tuple(map(int,m)) for m in re.findall(r'network70 frame=(\d+) history=(\d+) values=(\d+) different=(\d+)',log)]
assert rows==[(i,i%2,6635520,0) for i in range(5)] and 'extracted_network70=exact frames=5;' in log
outputs={}
for name,reference in [('gpu-network70.f32','release/native-rgb-valid1080/post70/shift-full-oracle.f32'),('gpu-network70-temporal.f32','release/native-temporal-valid1080/post70/shift-full-oracle.f32')]:
 raw=(root/name).read_bytes();assert raw==Path(reference).read_bytes() and len(raw)==6635520*4
 assert np.isfinite(np.frombuffer(raw,np.float32)).all();outputs[name]=hashlib.sha256(raw).hexdigest()
profiles=[];stages=[]
for line in log.splitlines():
 m=re.fullmatch(r'network_gpu_interval stage=(\S+) ms=([\d.]+)',line)
 if m:stages.append((m[1],float(m[2])))
 m=re.fullmatch(r'network_gpu_interval total_ms=([\d.]+) includes_inter_submission_gaps=1',line)
 if m:
  total=float(m[1]);assert stages and abs(sum(v for _,v in stages)-total)<.001
  profiles.append(dict(total_ms=total,stages=stages));stages=[]
assert len(profiles)==5 and not stages
names=[k for k,_ in profiles[0]['stages']]
assert len(names)==len(set(names)) and all([k for k,_ in f['stages']]==names for f in profiles)
means=np.mean([[v for _,v in f['stages']] for f in profiles[1:]],axis=0)
report=dict(scope='GPU timestamp intervals; cross-submission idle gaps included; warm frames mix history on/off',
 outputs_exact=outputs,frames=profiles,warm_ranked=sorted(zip(names,map(float,means)),key=lambda x:-x[1]),game_verified=False)
(root/'profile-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('five frames exact; warm intervals:',report['warm_ranked'][:8])
