"""Original same-RGB encoder5..8 main comparisons; down8 remains separate."""
from pathlib import Path
import json
import numpy as np
from native_c64_reference import block,unpack
from decode_tinlayout_global import e4m3fn
base=Path('release/native-rgb-valid1080');root=base/'encoder-c64';h,w=288,480;C=64;n=h*w*C
raw=np.fromfile(base/'encoder-c32/block4-down.fp8',np.uint8)[:n]
c=np.arange(C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
x=e4m3fn(raw).reshape(4,h,w,16).transpose(1,2,0,3).reshape(h,w,C)[...,perm]
inv=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc']);checks=[]
for b,s in ((5,0),(6,3),(7,1),(8,2)):
 raw=np.fromfile(root/f'block{b}-main.fp8',np.uint8);assert not raw[n:].any() and not np.any((raw[:n]&127)==127)
 actual=e4m3fn(raw[:n].reshape(-1,1024)[:,inv]).reshape(h//4,w//4,4,4,C).transpose(0,2,1,3,4).reshape(h,w,C)
 px=4 if s&1 else 0;py=4 if s&2 else 0;hh=h+2*py;ww=w+2*px
 padded=np.pad(x,((py,py),(px,px),(0,0)));tiles=padded.reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
 expected=block(tiles,*unpack(root/f'block{b}.weights')).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(hh,ww,C)[py:py+h,px:px+w]
 r={'block':b,'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())};checks.append(r);print(r,flush=True)
 if r['different']:
  locations=np.argwhere(actual!=expected)
  r['mismatches']=[{'yxc':idx.tolist(),'original':float(actual[tuple(idx)]),'reference':float(expected[tuple(idx)])} for idx in locations[:32]]
 (root/'main-validation.json').write_text(json.dumps({'scope':'original/CPU main5..8 same RGB; down8 not checked','checks':checks},indent=2)+'\n');assert r['different']==0 and r['finite']
 actual.tofile(root/f'block{b}-main.f32');x=actual
