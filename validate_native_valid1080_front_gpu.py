"""Exact original edge-oracle checks for same-RGB GPU0..4 chain."""
from pathlib import Path
import json
import numpy as np
root=Path('release/native-rgb-valid1080/amd-front4');checks=[]
for name,target,count in [('main','oracle-main4.f32',960*576*32),('down','oracle-down.f32',480*288*64)]:
 a=np.fromfile(root/f'gpu-{name}.f32',np.float32);b=np.fromfile(root/target,np.float32);assert a.size==b.size==count
 checks.append({'branch':name,'values':count,'different':int(np.count_nonzero(a!=b)),'max_abs':float(np.abs(a-b).max()),'finite':bool(np.isfinite(a).all() and np.isfinite(b).all())})
r={'scope':'GPU reflect/preblock/encoder1..4 same controlled valid1080 RGB, seed0; not game full texture contract','checks':checks,'pass':all(c['different']==0 and c['finite'] for c in checks)}
(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));assert r['pass']
