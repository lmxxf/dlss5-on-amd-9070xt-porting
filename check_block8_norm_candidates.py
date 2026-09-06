"""Normalization operation-order diagnostics; not a production selection rule."""
from pathlib import Path
import json
import numpy as np
import native_c64_reference as ref
from native_c32_reference import H
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64)
target=np.fromfile(root/'attention-original.f32',np.float32).reshape(x.shape);params=ref.unpack(root/'attention-control.weights');old=ref.normalize;checks=[]
def norm(v,mode):
 a,b=v[...,:16],v[...,16:]
 if mode=='both-rounded':s=H(H(a*a)+H(b*b))
 elif mode=='lower-rounded':s=H(H(a*a)+b*b)
 else:s=H(a*a+H(b*b))
 s=H(s[...,::2]+s[...,1::2])
 for w in (4,2,1):s=H(s[...,:w]+s[...,w:2*w])
 return H(v*H(1/np.sqrt(np.maximum(s,6.198883056640625e-5))))
try:
 for mode in ('baseline','both-rounded','lower-rounded'):
  ref.normalize=lambda v: norm(v,mode)
  got=ref.block(x,*params);checks.append({'mode':mode,'different':int(np.count_nonzero(got!=target)),'query18_different':int(np.count_nonzero(got[:,18]!=target[:,18]))})
finally:ref.normalize=old
(root/'norm-candidates.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2))
