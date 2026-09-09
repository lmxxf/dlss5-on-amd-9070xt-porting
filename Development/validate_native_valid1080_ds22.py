"""Same-RGB block22 raw pool and native C256->C512 downsample check."""
from pathlib import Path
import json,numpy as np
from native_c64_reference import block,unpack,multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c256');h,w,C=72,120,256
x=np.fromfile(root/'block21-main.f32',np.float32).reshape(h,w,C)
padded=np.pad(x,((4,4),(0,0),(0,0)));hh=h+8
tiles=padded.reshape(hh//8,8,w//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
raw=block(tiles,*unpack(root/'block22.weights'),raw_output=True).reshape(hh//8,w//8,8,8,C).transpose(0,2,1,3,4).reshape(hh,w,C)[4:4+h]
pooled=F(H(H(H(raw[::2,::2]+raw[::2,1::2])+H(raw[1::2,::2]+raw[1::2,1::2]))*.25))
layout=np.load('release/native-c256/ds-layout/layout.npz');weights=np.fromfile(root/'block22.weights',np.uint8)
matrix=np.empty((512,256),np.float32);matrix[layout['output'],layout['input']]=e4m3fn(weights[0xa8440:])
expected=F(multiply(pooled,matrix));v=np.fromfile(root/'block22-down.fp8',np.uint8);n=36*60*512
assert not v[n:].any() and not np.any((v[:n]&127)==127)
physical=e4m3fn(v[:n]).reshape(32,36,60,16).transpose(1,2,0,3).reshape(36,60,512)
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1);actual=physical[...,perm]
r={'scope':'original/CPU block22 down same valid1080 RGB, not GPU full encoder','values':n,'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())}
(root/'down-validation.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));assert r['different']==0 and r['finite']
actual.tofile(root/'block22-down.f32');matrix.tofile(root/'block22-ds.f32')
