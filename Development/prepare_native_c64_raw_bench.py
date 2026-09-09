"""Small raw-output regression against the independent CPU reference.

This checks optimization equivalence, not original-CUDA raw-output acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from native_c64_reference import block, unpack

p = argparse.ArgumentParser()
p.add_argument('--base', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--channels', type=int, choices=(64,128,256), required=True)
a = p.parse_args()
C = a.channels
b = {64:5,128:9,256:15}[C]
weights = a.base / f'encoder-c{C}/block{b}.weights'
params = unpack(weights)
# Exact E4M3 inputs: asymmetric signs and spatial/channel patterns.
rng = np.random.default_rng(297+C)
x = rng.integers(-16,17,size=(1,64,C)).astype(np.float32)/16
y = block(x,*params,raw_output=True)
assert np.isfinite(y).all()
assert np.any(y != y.astype(np.float16).astype(np.float32)) == False
a.output.mkdir(parents=True,exist_ok=False)
x.astype('<f4').tofile(a.output/'input.f32')
y.astype('<f4').tofile(a.output/'oracle.f32')
ffn,qkv,projection,bias,scales,skip = params
np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(a.output/'ffn.f32')
np.concatenate([*[m.ravel() for m in qkv],projection.ravel(),bias.ravel(),scales,skip]).astype('<f4').tofile(a.output/'attention.f32')
(a.output/'geometry.txt').write_text(f'8 8 {C} 1\n')
(a.output/'lineage.json').write_text(json.dumps({'scope':'CPU raw reference plus archived GPU baseline regression; not direct CUDA raw oracle','weights':str(weights),'sha256':hashlib.sha256(weights.read_bytes()).hexdigest(),'seed':297+C},indent=2)+'\n')
