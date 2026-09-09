"""Package verified original actual-size block56 for isolated AMD testing."""
from pathlib import Path
import json,argparse
import numpy as np
from native_upsample48_reference import unpack
p=argparse.ArgumentParser();p.add_argument('--block62',action='store_true');a=p.parse_args();b=62 if a.block62 else 56
root=Path(f'release/native-upsample{b}/game');out=Path(f'release/native-upsample{b}/amd-game')
r=json.loads((root/'validation.json').read_text());assert r['different']==0 and r['finite'] and r['tail_zero']
factor=2 if a.block62 else 1
assert (root/'input.f32').stat().st_size==120*72*256*4*factor and (root/'oracle.f32').stat().st_size==240*144*128*4*factor
out.mkdir(exist_ok=False)
for src,dst in [('input.f32','input.f32'),('skip.f32','residual.f32'),('oracle.f32','oracle.f32')]: (out/dst).write_bytes((root/src).read_bytes())
matrix,scale,body=unpack(root/'weights.bin');np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(out/'weights.f32')
ffn,qkv,projection,bias,scales,skip=body
np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(out/'ffn.f32')
np.concatenate([*[m.ravel() for m in qkv],projection.ravel(),bias.ravel(),scales,skip]).astype('<f4').tofile(out/'attention.f32')
