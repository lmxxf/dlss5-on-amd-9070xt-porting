"""Compare actual-size original post RGB to native reference in aligned strips."""
from pathlib import Path
import json
import numpy as np
from native_post70_reference import post,unpack
root=Path('release/native-post70/game');h,w=1152,1920
main=np.memmap(root/'main.f32',dtype='<f4',mode='r',shape=(h//2,w//2,32))
skip=np.memmap(root/'skip.f32',dtype='<f4',mode='r',shape=(h,w,32))
color=np.memmap(root/'color.f32',dtype='<f4',mode='r',shape=(h,w,4))
target=np.memmap(root/'output.f32',dtype='<f4',mode='r',shape=(h,w,4))
params=unpack('release/native-post70/smoke/weights.bin');checks=[]
# Shift0 post has independent8x8 windows. Aligned16-row strips preserve those windows.
for y in range(0,h,16):
 expected=post(main[y//2:y//2+8],skip[y:y+16],color[y:y+16,:,:3],params)
 actual=target[y:y+16,:,:3];delta=np.abs(expected-actual)
 checks.append({'y':y,'different':int(np.count_nonzero(delta)),'max_abs':float(delta.max()),'finite':bool(np.isfinite(expected).all() and np.isfinite(actual).all())})
 if y%128==0:print(checks[-1],flush=True)
report={'scope':'original/CPU actual post70 random inputs; not AMD/full RGB chain','values':h*w*3,'different':sum(c['different'] for c in checks),'max_abs':max(c['max_abs'] for c in checks),'finite':all(c['finite'] for c in checks),'strips':checks}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print({k:v for k,v in report.items() if k!='strips'})
assert report['different']==0 and report['finite']
np.asarray(target[:,:,:3]).copy().tofile(root/'oracle.f32')
