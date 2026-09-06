"""Check native block4 pool/projection from raw half output, not FP8 main."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import block,unpack,H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c32');h,w=576,960
x=np.fromfile(root/'block3-main.f32',np.float32).reshape(h,w,32)
padded=np.pad(x,((4,4),(0,0),(0,0)));hh=h+8
tiles=padded.reshape(hh//8,8,w//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
raw=block(tiles,unpack(root/'block4.weights'),raw_output=True).reshape(hh//8,w//8,8,8,32).transpose(0,2,1,3,4).reshape(hh,w,32)[4:4+h]
pooled=H(H(H(raw[::2,::2]+raw[::2,1::2])+H(raw[1::2,::2]+raw[1::2,1::2]))*.25)
matrix=np.fromfile('release/native-rgb512/amd/block4-ds.f32',np.float32).reshape(64,32)
expected=F(H(pooled@matrix.T))
packed=np.fromfile(root/'block4-down.fp8',np.uint8);n=h//2*(w//2)*64;assert not packed[n:].any() and not np.any((packed[:n]&127)==127)
actual=e4m3fn(packed[:n]).reshape(4,h//2,w//2,16).transpose(1,2,0,3).reshape(h//2,w//2,64)
r={'scope':'original/CPU actual block4 downsample, not AMD full encoder','values':n,'different':int(np.count_nonzero(expected!=actual)),'max_abs':float(np.abs(expected-actual).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())}
print(json.dumps(r,indent=2));(root/'down-validation.json').write_text(json.dumps(r,indent=2)+'\n');assert r['different']==0 and r['finite']
actual.tofile(root/'block4-down.f32')
