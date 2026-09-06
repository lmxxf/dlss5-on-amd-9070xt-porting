"""Pack a QKV -> attention test with the original RTX final output."""
from pathlib import Path
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
root=Path('release/native-vit/qkv-attention-256-3003')
source=Path('release/native-vit/qkv-extent-256-3003')
n=256
raw=np.fromfile(root/'rtx-output-1.fp8',np.uint8)
assert (root/'rtx-output-1.fp8').read_bytes()==(root/'rtx-output-2.fp8').read_bytes()
assert raw.size==4194304 and not raw[n*1024:].any()
actual=np.empty((n,1024),np.float32)
actual[bits(n*1024,[2,6,7,8,14,15,16,17]),bits(n*1024,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(raw[:n*1024])
assert np.isfinite(actual).all()
actual.tofile(root/'oracle.f32')
for name in ('input.f32','weights.f32'):(root/name).write_bytes((source/name).read_bytes())
