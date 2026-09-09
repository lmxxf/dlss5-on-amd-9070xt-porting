"""Export validated native split parameters and original per-stage test oracles."""
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512');out=root/'amd';out.mkdir(parents=True,exist_ok=True)
fw=np.load(root/'ffwd-check/matrices.npz');fp=np.load(root/'projection-check/matrices.npz');attn=np.load(root/'full-check/attention-matrices.npz')
np.concatenate([fw[k].ravel() for k in ('pre','expand','contract')]).astype('<f4').tofile(out/'ffwd.f32')
np.concatenate([fp[k].ravel() for k in ('matrix','skip')]).astype('<f4').tofile(out/'ffwd-projection.f32')
np.concatenate([attn[k].ravel() for k in ('Q','K','V','P','bias','scales','skip')]).astype('<f4').tofile(out/'attention.f32')
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
x=e4m3fn(np.fromfile(root/'full-check/input.fp8',np.uint8)).reshape(32,8,16,16).transpose(1,2,0,3).reshape(8,16,512)[...,perm]
x.astype('<f4').tofile(out/'input.f32')
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
for i,suffix in enumerate(('.branch','.ffn','.attn','')):
 raw=np.fromfile(str(root/'full-check/output.fp8')+suffix,np.uint8);assert not np.any(raw[65536:]) and not np.any((raw[:65536]&127)==127)
 target=e4m3fn(raw[:65536].reshape(8,8192)[:,inverse]).reshape(2,4,4,4,512).transpose(0,2,1,3,4).reshape(8,16,512)
 target.astype('<f4').tofile(out/f'oracle-{i}.f32')
alternate=np.load(root/'full-check/fixture-661.npz')
alternate['input'].astype('<f4').tofile(out/'input-alt.f32')
for i in range(4):alternate[f'oracle_{i}'].astype('<f4').tofile(out/f'oracle-alt-{i}.f32')
