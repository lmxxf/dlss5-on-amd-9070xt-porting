from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--size',type=int,choices=[128,256],default=128);args=parser.parse_args()
root=Path(f'release/native-rgb{args.size}');out=root/'amd';out.mkdir(parents=True,exist_ok=True)
for path in Path('release/native-front-chain').glob('block*-*.f32'):(out/path.name).write_bytes(path.read_bytes())
(out/'input.rgba32f').write_bytes((root/'input-tiles.rgba32f').read_bytes())
for name in ('ffwd','ffwd-projection','attention'):
 (out/f'block23-{name}.f32').write_bytes(Path(f'release/native-c512/amd/{name}.f32').read_bytes())
import numpy as np
from native_split_weights import unpack
for block in range(24,31 if args.size==256 else 30):
 fw,fp,qkv,bias,scales,projection=unpack('release/native-c512',block)
 np.concatenate([fw[k].ravel() for k in ('pre','expand','contract')]).astype('<f4').tofile(out/f'block{block}-ffwd.f32')
 np.concatenate([fp['matrix'].ravel(),fp['skip']]).astype('<f4').tofile(out/f'block{block}-ffwd-projection.f32')
 np.concatenate([*[m.ravel() for m in qkv],projection['matrix'].ravel(),bias.ravel(),scales,projection['skip']]).astype('<f4').tofile(out/f'block{block}-attention.f32')
if args.size==256:(out/'head-matrix.f32').write_bytes((root/'head-matrix.f32').read_bytes())
