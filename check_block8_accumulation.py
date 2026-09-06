"""Diagnostic: exact float64 inner dot sums versus current float32 BLAS reference."""
from pathlib import Path
import json
import numpy as np
import native_c64_reference as ref
from native_c32_reference import H
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64);target=np.fromfile(root/'oracle.f32',np.float32).reshape(x.shape)
params=ref.unpack(root.parent/'block8.weights');original=ref.multiply
def precise(a,m,initial=None):
 result=np.zeros((*a.shape[:-1],m.shape[0]),np.float32) if initial is None else initial.copy()
 for off in range(0,m.shape[1],32):
  dot=a[...,off:off+32].astype(np.float64)@m[:,off:off+32].astype(np.float64).T
  result=H(result.astype(np.float64)+dot)
 return result
normalizer=ref.normalize
def precise_norm(value):
 sums=H(value[...,:16].astype(np.float64)**2+H(value[...,16:]*value[...,16:]))
 sums=H(sums[...,::2]+sums[...,1::2])
 for width in (4,2,1):sums=H(sums[...,:width]+sums[...,width:2*width])
 inv=H(1/np.sqrt(np.maximum(sums,6.198883056640625e-5).astype(np.float64)))
 return H(value*inv)
checks=[]
try:
 for name,fn,norm in [('float32_dot',original,normalizer),('float64_dot',precise,normalizer),('float64_fused_square_and_rsqrt',precise,precise_norm)]:
  ref.multiply=fn;ref.normalize=norm;got=ref.block(x,*params);idx=np.argwhere(got!=target)
  checks.append({'candidate':name,'different':len(idx),'max_abs':float(np.abs(got-target).max()),'mismatches':[{'index':i.tolist(),'original':float(target[tuple(i)]),'reference':float(got[tuple(i)])} for i in idx[:16]]})
finally:ref.multiply=original;ref.normalize=normalizer
(root/'accumulation-candidates.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2))
