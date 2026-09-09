"""Compare GPU reflect->preblock branch outputs to original valid1080 outputs."""
from pathlib import Path
import json
import numpy as np
root=Path('release/native-rgb-valid1080/amd-preblock');checks=[]
for name,count in [('main',1152*1920*32),('down',576*960*32)]:
 actual=np.fromfile(root/f'gpu-{name}.f32',np.float32);expected=np.fromfile(root/f'{name}-oracle.f32',np.float32)
 assert actual.size==expected.size==count
 checks.append({'branch':name,'values':count,'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max())})
r={'scope':'GPU valid1080 reflect->preblock seed0 dual branch; not full RGB/game texture contract','checks':checks,'pass':all(c['finite'] and c['different']==0 for c in checks)}
(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));assert r['pass']
