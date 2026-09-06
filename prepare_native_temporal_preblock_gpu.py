"""Isolated preblock integration fixture; temporal sampler result supplied by CPU."""
from pathlib import Path
import numpy as np
from native_temporal_sampling_reference import sample32
root=Path('release/native-temporal-inputs-gates');out=root/'amd-preblock';out.mkdir(exist_ok=False)
for name in ('ffn','attention'):
 (out/f'block0-{name}.f32').write_bytes((Path('release/native-rgb-valid1080/amd-full')/f'block0-{name}.f32').read_bytes())
(out/'input.f32').write_bytes((root/'rgb.f32').read_bytes())
history=np.fromfile(root/'history.f32',np.float32).reshape(8,8,4)
y,x=np.mgrid[:8,:8];sampled=np.ones((8,8,4),np.float32);sampled[:,:,:3]=sample32(history[:,:,:3],x+.625,y+.625)
sampled.tofile(out/'temporal.f32')
