"""Localize valid1080 versus full1152 texture differences in logical pixels."""
from pathlib import Path
import json
import numpy as np
root=Path('release/native-rgb-valid1080');old=Path('release/native-rgb-game');h,w=1152,1920
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)
mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
results={}
for name in ('main','down'):
 arrays=[np.fromfile(p/f'block0-{name}.fp8',np.uint8) for p in (old,root)]
 if name=='main':
  arrays=[a.reshape(-1,512)[:,mapping].reshape(h//4,w//4,4,4,32).transpose(0,2,1,3,4).reshape(h,w,32) for a in arrays];valid=1080
 else:
  arrays=[a.reshape(2,h//2,w//2,16).transpose(1,2,0,3).reshape(h//2,w//2,32) for a in arrays];valid=540
 delta=arrays[0]!=arrays[1];rows=delta.sum(axis=(1,2));ys=np.flatnonzero(rows)
 results[name]={'different_values':int(rows.sum()),'first_changed_row':int(ys[0]) if len(ys) else None,'last_changed_row':int(ys[-1]) if len(ys) else None,'valid_region_differences':int(rows[:valid].sum()),'padding_region_differences':int(rows[valid:].sum()),'per_row':rows.tolist()}
report={'scope':'original output spatial difference audit; no AMD acceptance','branches':results}
(root/'spatial-difference.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:{x:y for x,y in v.items() if x!='per_row'} for k,v in results.items()},indent=2))
