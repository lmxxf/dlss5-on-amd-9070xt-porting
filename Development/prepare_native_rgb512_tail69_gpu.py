"""Export verified original decoder49..69 coefficients for resident integration."""
from pathlib import Path
import json
import numpy as np
from native_upsample48_reference import unpack
from native_upsample66_reference import unpack as unpack66
root=Path('release/native-rgb512');out=root/'amd-tail69';out.mkdir(exist_ok=False)
for index in [*range(49,56),*range(57,62),*range(63,66),*range(67,70)]:
    source=root/f'decoder-block{index}';report=json.loads((source/'validation.json').read_text())
    assert report['status']=='pass' and report['different']==0
    for name in ('ffn','attention'):(out/f'block{index}-{name}.f32').write_bytes((source/f'{name}.f32').read_bytes())
for index in (56,62,66):
    source=root/f'upsample{index}-shift0';report=json.loads((source/'validation.json').read_text())
    assert report['status']=='pass' and report['different']==0 and report['shift']==0
    matrix,scale,body=unpack66('release/native-upsample66/weights.bin') if index==66 else unpack(source/'weights.bin')
    np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(out/f'block{index}-weights.f32')
    if index==66:
        w1,w2,q,k,v,p,bias,scale,fs,ats=body
        fw=np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]);aw=np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[scale],ats])
    else:
        ffn,qkv,p,bias,scales,skip=body
        fw=np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]);aw=np.concatenate([*[m.ravel() for m in qkv],p.ravel(),bias.ravel(),scales,skip])
    fw.astype('<f4').tofile(out/f'block{index}-ffn.f32');aw.astype('<f4').tofile(out/f'block{index}-attention.f32')
