"""Validate original encoder1..4 main outputs from the same valid1080 input."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import block,unpack
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c32');w,h=960,576;n=w*h*32
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
raw=np.fromfile('release/native-rgb-valid1080/block0-down.fp8',np.uint8);assert raw.size==n
x=e4m3fn(raw).reshape(2,h,w,16).transpose(1,2,0,3).reshape(h,w,32);checks=[]
for b,shift in ((1,0),(2,3),(3,1),(4,2)):
 raw=np.fromfile(root/f'block{b}-main.fp8',np.uint8);assert not raw[n:].any() and not np.any((raw[:n]&127)==127)
 actual=e4m3fn(raw[:n].reshape(-1,512)[:,mapping]).reshape(h//4,w//4,4,4,32).transpose(0,2,1,3,4).reshape(h,w,32)
 px=4 if shift&1 else 0;py=4 if shift&2 else 0
 padded=np.pad(x,((py,py),(px,px),(0,0)));hh,ww=padded.shape[:2]
 tiles=padded.reshape(hh//8,8,ww//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
 expected=block(tiles,unpack(root/f'block{b}.weights')).reshape(hh//8,ww//8,8,8,32).transpose(0,2,1,3,4).reshape(hh,ww,32)[py:py+h,px:px+w]
 r={'block':b,'different':int(np.count_nonzero(actual!=expected)),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())};checks.append(r);print(r,flush=True)
 (root/'main-validation.json').write_text(json.dumps({'scope':'original/CPU main1..4; block4 down not yet checked','checks':checks},indent=2)+'\n')
 assert r['different']==0 and r['finite']
 actual.tofile(root/f'block{b}-main.f32');x=actual
