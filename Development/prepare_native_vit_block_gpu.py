"""Assemble already decoded block31 weights and original complete-layer oracle."""
from pathlib import Path
import json,argparse
import numpy as np
from native_split_reference import bits
from native_vit_linear_reference import unpack_expand,unpack_residual
from native_vit_qkv_reference import unpack as unpack_qkv
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--chain',action='store_true');parser.add_argument('--alternate',action='store_true');args=parser.parse_args()
if args.alternate and not args.chain:parser.error('--alternate requires --chain')
base=Path('release/native-vit');report=json.loads((base/'chain31-38-64-2107/validation.json').read_text())
assert report['status']=='pass' and len(report['stages'])==56 and all(s['different']==0 for s in report['stages'])
out=base/('amd-chain' if args.chain else 'amd-block31');out.mkdir(exist_ok=True);job=base/'chain31-38-64-2107'
def decode(path):
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[65536:]) and not np.any((raw[:65536]&127)==127)
 value=np.empty((64,1024),np.float32);value[bits(65536,[2,6,7,8,14,15]),bits(65536,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(raw[:65536]);return value
decode(job/'input.fp8').tofile(out/'input.f32');decode(job/f'block{38 if args.chain else 31}/projection.fp8').tofile(out/'oracle.f32')
if args.alternate:
 alt=base/'chain31-38-64-2101';report=json.loads((alt/'validation.json').read_text());assert report['status']=='pass' and len(report['stages'])==56 and all(s['different']==0 for s in report['stages'])
 decode(alt/'input.fp8').tofile(out/'input-alt.f32');decode(alt/'block38/projection.fp8').tofile(out/'oracle-alt.f32')
for block in range(31,39 if args.chain else 32):
 prefix=f'block{block}-' if args.chain else ''
 unpack_expand(base/f'block{block}-expand.weights').tofile(out/f'{prefix}expand.f32')
 for kind,inputs in [('contract',4096),('projection',1024)]:
  matrix,skip=unpack_residual(base/f'block{block}-{kind}.weights',inputs);np.concatenate([matrix.ravel(),skip]).tofile(out/f'{prefix}{kind}.f32')
 matrices,scales=unpack_qkv(base/f'block{block}-qkv.weights');np.concatenate([*[m.ravel() for m in matrices],scales]).tofile(out/f'{prefix}qkv.f32')
