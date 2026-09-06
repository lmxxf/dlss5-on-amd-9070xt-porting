"""Validate original 256-token contraction including native residual arithmetic."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
from native_vit_linear_reference import residual_projection,unpack_residual
root=Path('release/native-vit/contract-256-3003');n=256
branch=np.fromfile('release/native-vit/expand-256-3003/oracle.f32',np.float32).reshape(n,4096)
residual=np.load('release/native-vit/attention-random-256-3003/logical.npz')['q']
raw=np.fromfile(root/'contract-1.fp8',np.uint8)
actual=np.empty((n,1024),np.float32)
actual[bits(n*1024,[2,6,7,8,14,15,16,17]),bits(n*1024,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(raw[:n*1024])
weight,skip=unpack_residual('release/native-vit/block31-contract.weights',4096)
expected=residual_projection(branch,residual,weight,skip)
report={'tokens':n,'values':actual.size,'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'tail_zero':not bool(raw[n*1024:].any()),'replay_identical':(root/'contract-1.fp8').read_bytes()==(root/'contract-2.fp8').read_bytes(),'scope':'original contract only; not AMD or game acceptance'}
print(json.dumps(report,indent=2));(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['different']==0 and report['finite'] and report['tail_zero'] and report['replay_identical']
branch.tofile(root/'input.f32');residual.tofile(root/'residual.f32');actual.tofile(root/'oracle.f32')
np.concatenate([weight.ravel(),skip]).astype('<f4').tofile(root/'weights.f32')
