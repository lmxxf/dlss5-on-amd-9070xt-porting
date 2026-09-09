"""Replay reference QK+bias as bias with Q zero, isolating score generation."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_c64_reference import contract
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');d=np.load(root/'query-reference.npz')
raw=np.fromfile(root/'attention-control.weights',np.uint8).copy()
layout=np.load('release/native-c64/attention-layout/matrix-layout.npz')
rows=layout['v_output'];raw[(layout['v_offsets']-0x800)[rows>=32]]=0
bl=np.load('release/native-c64/attention-layout/bias-layout.npz');_,scale,_,bias,_=contract(64)
values=raw[bias:scale].view('<f2');mask=bl['head']==1
values[mask]=d['scores'][0,bl['query'][mask],bl['key'][mask]]
weights=root/'score-replay.weights';raw.tofile(weights)
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(weights),str(root/'input.fp8'),str(root/'score-replay.fp8'),str(root/'score-replay-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_fp8','8','8','1','1','2','7','0'],check=True,timeout=20)
inv=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc']);out=np.fromfile(root/'score-replay.fp8',np.uint8)
actual=e4m3fn(out[:4096].reshape(-1,1024)[:,inv]).reshape(2,2,4,4,64).transpose(0,2,1,3,4).reshape(1,64,64)
original=np.fromfile(root/'attention-original.f32',np.float32).reshape(actual.shape);reference=np.fromfile(root/'attention-reference.f32',np.float32).reshape(actual.shape)
r={'scope':'diagnostic replay of CPU scores through original softmax/AV; not model acceptance','different_from_original_attention':int(np.count_nonzero(actual!=original)),'different_from_reference_attention':int(np.count_nonzero(actual!=reference))}
(root/'score-replay.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
