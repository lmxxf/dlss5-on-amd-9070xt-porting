"""Prepare isolated block30 GPU test, using original input/output only at edges."""
from pathlib import Path
import numpy as np
from native_split_weights import unpack
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb256');out=Path('release/native-c512/amd-pool-head');out.mkdir(exist_ok=True)
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
raw=np.fromfile(root/'block29-main.fp8',np.uint8);assert not np.any(raw[32768:])
x=e4m3fn(raw[:32768].reshape(-1,8192)[:,inverse]).reshape(2,2,4,4,512).transpose(0,2,1,3,4).reshape(8,8,512)
x.astype('<f4').tofile(out/'input.f32')
raw=np.fromfile(root/'block30-head.fp8',np.uint8);assert not np.any(raw[16384:]) and not np.any((raw[:16384]&127)==127)
target=e4m3fn(raw[:16384]).reshape(2,8192)[:,inverse].reshape(2,4,4,512).transpose(1,2,0,3).reshape(4,4,1024)
target.astype('<f4').tofile(out/'oracle.f32')
fw,fp,qkv,bias,scales,projection=unpack('release/native-c512',30)
np.concatenate([fw[k].ravel() for k in ('pre','expand','contract')]).astype('<f4').tofile(out/'ffwd.f32')
np.concatenate([fp['matrix'].ravel(),fp['skip']]).astype('<f4').tofile(out/'ffwd-projection.f32')
np.concatenate([*[m.ravel() for m in qkv],projection['matrix'].ravel(),bias.ravel(),scales,projection['skip']]).astype('<f4').tofile(out/'attention.f32')
(out/'head.f32').write_bytes((root/'head-matrix.f32').read_bytes())
