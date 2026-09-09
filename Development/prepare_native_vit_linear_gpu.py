"""Prepare independent AMD linear-stage checks from the verified original chain."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from native_vit_linear_reference import unpack_expand,unpack_residual
from decode_tinlayout_global import e4m3fn
base=Path('release/native-vit');job=base/'chain31-38-64-2107';root=job/'block31'
report=json.loads((job/'validation.json').read_text());assert report['status']=='pass' and len(report['stages'])==56 and all(s['different']==0 for s in report['stages'])
def decode(path,C):
 n=64*C;raw=np.fromfile(path,np.uint8);assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 t=bits(n,[2,6,7,8,C.bit_length()+3,C.bit_length()+4]);c=bits(n,[0,1,3,4,5]+list(range(9,C.bit_length()+3)))
 result=np.empty((64,C),np.float32);result[t,c]=e4m3fn(raw[:n]);return result
for mode,inputs,outputs,source,residual,target in [('expand',1024,4096,job/'input.fp8',None,root/'expand.fp8'),('contract',4096,1024,root/'expand.fp8',job/'input.fp8',root/'contract.fp8'),('projection',1024,1024,root/'attention.fp8',root/'contract.fp8',root/'projection.fp8')]:
 out=base/'amd-linear'/mode;out.mkdir(parents=True,exist_ok=True)
 decode(source,inputs).tofile(out/'input.f32');decode(target,outputs).tofile(out/'oracle.f32')
 if residual is not None:decode(residual,1024).tofile(out/'residual.f32')
 if mode=='expand':weights=unpack_expand(base/'block31-expand.weights').ravel()
 else:
  matrix,skip=unpack_residual(base/f'block31-{mode}.weights',inputs);weights=np.concatenate([matrix.ravel(),skip])
 weights.astype('<f4').tofile(out/'weights.f32')
