from pathlib import Path
import json,numpy as np
from native_c32_reference import F
root=Path('release/native-rgb-valid1080/amd-front8');checks=[]
for name,count,target in [('main',480*288*64,'oracle-main8.f32'),('down',240*144*128,'oracle-down.f32')]:
 a=np.fromfile(root/f'gpu-{name}.f32',np.float32);b=np.fromfile(root/target,np.float32);assert a.size==b.size==count and np.isfinite(a).all() and np.isfinite(b).all()
 if name=='main':a=F(a) # Production retains raw half for pool; original main is FP8.
 checks.append({'branch':name,'values':count,'different':int(np.count_nonzero(a!=b)),'max_abs':float(np.abs(a-b).max())})
r={'scope':'same controlled valid1080 RGB GPU0..8 seed0; not game full-texture contract','checks':checks,'pass':all(c['different']==0 for c in checks)}
(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));assert r['pass']
