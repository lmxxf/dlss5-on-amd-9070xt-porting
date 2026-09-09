"""Full-size shifted post comparison with aligned eight-row halos."""
from pathlib import Path
import json,numpy as np
from native_post70_reference import post,unpack
root=Path('release/native-temporal-valid1080/post70');h,w=1152,1920
main=np.memmap(root/'main.f32',np.float32,'r',shape=(h//2,w//2,32))
skip=np.memmap(root/'skip.f32',np.float32,'r',shape=(h,w,32))
color=np.memmap(root/'color.f32',np.float32,'r',shape=(h,w,4))
actual=np.memmap(root/'origin-minus4-full.f32',np.float32,'r',shape=(h,w,4))
params=unpack('release/native-post70/smoke/weights.bin');checks=[]
for y in range(0,h,16):
 lo,hi=max(0,y-8),min(h,y+24)
 s=np.zeros((32,w,32),np.float32);m=np.zeros((16,w//2,32),np.float32);c=np.zeros((32,w,3),np.float32)
 offset=lo-(y-8);s[offset:offset+hi-lo]=skip[lo:hi];c[offset:offset+hi-lo]=color[lo:hi,:,:3]
 m[offset//2:offset//2+(hi-lo)//2]=main[lo//2:hi//2]
 expected=post(m,s,c,params,origin=(-4,-4))[8:24]
 target=actual[y:y+16,:,:3]
 checks.append({'y':y,'different':int(np.count_nonzero(expected!=target)),'max_abs':float(np.abs(expected-target).max()),'finite':bool(np.isfinite(expected).all() and np.isfinite(target).all())})
 if y%128==0:print(checks[-1],flush=True)
report={'scope':'full processing size origin(-4,-4), reflected1152 color fixture; not live1080 texture contract',
 'values':h*w*3,'different':sum(r['different'] for r in checks),'max_abs':max(r['max_abs'] for r in checks),'finite':all(r['finite'] for r in checks),'strips':checks}
(root/'shift-full-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print({k:v for k,v in report.items() if k!='strips'})
assert report['different']==0 and report['finite']
np.asarray(actual[:,:,:3]).copy().tofile(root/'shift-full-oracle.f32')
