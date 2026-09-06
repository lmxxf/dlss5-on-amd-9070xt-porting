from pathlib import Path
import numpy as np
from native_c64_reference import unpack
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');out=root/'amd';out.mkdir(exist_ok=False)
for a,b in [('input.f32','input.f32'),('oracle.f32','oracle-0.f32')]:(out/b).write_bytes((root/a).read_bytes())
ffn,qkv,p,bias,sc,skip=unpack(root.parent/'block8.weights')
np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(out/'ffn.f32')
np.concatenate([*[m.ravel() for m in qkv],p.ravel(),bias.ravel(),sc,skip]).astype('<f4').tofile(out/'attention.f32')
