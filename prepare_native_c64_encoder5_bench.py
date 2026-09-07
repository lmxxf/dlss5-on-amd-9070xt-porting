"""Export the already verified original encoder5 fixture; never run the candidate."""
from pathlib import Path
import argparse
import hashlib
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
from native_c64_reference import unpack

p = argparse.ArgumentParser()
p.add_argument('--base', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = a.base / 'encoder-c64'
checks = json.loads((root / 'main-validation.json').read_text())['checks']
assert any(c['block'] == 5 and c['different'] == 0 and c['finite'] for c in checks)
w, h, C = 480, 288, 64
n = w*h*C
source = a.base / 'encoder-c32/block4-down.fp8'
original = root / 'block5-main.fp8'
raw = np.fromfile(source, np.uint8)
assert raw.size >= n and not raw[n:].any()
c = np.arange(C)
perm = (c & ~3) | ((c & 1) << 1) | ((c & 2) >> 1)
x = e4m3fn(raw[:n]).reshape(C//16,h,w,16).transpose(1,2,0,3).reshape(h,w,C)[...,perm]
raw = np.fromfile(original, np.uint8)
assert raw.size >= n and not raw[n:].any()
inv = np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
y = e4m3fn(raw[:n].reshape(-1,16*C)[:,inv]).reshape(h//4,w//4,4,4,C).transpose(0,2,1,3,4).reshape(h,w,C)
assert np.isfinite(x).all() and np.isfinite(y).all()
ffn,qkv,projection,bias,scales,skip = unpack(root / 'block5.weights')
a.output.mkdir(parents=True, exist_ok=False)
x.astype('<f4').tofile(a.output / 'input.f32')
y.astype('<f4').tofile(a.output / 'oracle.f32')
np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(a.output/'ffn.f32')
np.concatenate([*[m.ravel() for m in qkv],projection.ravel(),bias.ravel(),scales,skip]).astype('<f4').tofile(a.output/'attention.f32')
(a.output/'geometry.txt').write_text(f'{w} {h} {C} 0\n')
(a.output/'lineage.json').write_text(json.dumps({str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in (source,original,root/'block5.weights')}, indent=2)+'\n')
