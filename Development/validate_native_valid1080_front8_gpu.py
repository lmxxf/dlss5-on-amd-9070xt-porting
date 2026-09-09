from pathlib import Path
import json,numpy as np,argparse
from native_c32_reference import F
p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group();g.add_argument('--front14',action='store_true');g.add_argument('--front22',action='store_true');args=p.parse_args()
root=Path('release/native-rgb-valid1080')/('amd-front22' if args.front22 else 'amd-front14' if args.front14 else 'amd-front8');checks=[]
branches=[('main',240*144*128,'oracle-main14.f32'),('down',120*72*256,'oracle-down.f32')] if args.front14 else [('main',480*288*64,'oracle-main8.f32'),('down',240*144*128,'oracle-down.f32')]
if args.front22:branches=[('main',120*72*256,'oracle-main22.f32'),('down',60*36*512,'oracle-down.f32')]
for name,count,target in branches:
 a=np.fromfile(root/f'gpu-{name}.f32',np.float32);b=np.fromfile(root/target,np.float32);assert a.size==b.size==count and np.isfinite(a).all() and np.isfinite(b).all()
 if name=='main':a=F(a) # Production retains raw half for pool; original main is FP8.
 checks.append({'branch':name,'values':count,'different':int(np.count_nonzero(a!=b)),'max_abs':float(np.abs(a-b).max())})
r={'scope':'same controlled valid1080 RGB GPU encoder seed0; not game full-texture contract','last_block':22 if args.front22 else 14 if args.front14 else 8,'checks':checks,'pass':all(c['different']==0 for c in checks)}
(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));assert r['pass']
