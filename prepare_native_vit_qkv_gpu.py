"""Export logical QKV fixtures from independently verified original outputs."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from native_vit_qkv_reference import unpack
from decode_tinlayout_global import e4m3fn
base=Path('release/native-vit');job=base/'chain31-38-64-2107';root=job/'block31';out=base/'amd-qkv';out.mkdir(exist_ok=True)
report=json.loads((job/'validation.json').read_text());assert report['status']=='pass' and len(report['stages'])==56 and all(s['different']==0 for s in report['stages'])
def decode(path,part=None):
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[65536:]) and not np.any((raw[:65536]&127)==127)
 tb=[1,0,4,5,2,15] if part==2 else [3 if part==1 else 2,6,7,8,14,15]
 cb=[6,3,9,7,8,10,11,12,13,14] if part==2 else [0,1,2 if part==1 else 3,4,5,9,10,11,12,13]
 value=np.empty((64,1024),np.float32);value[bits(65536,tb),bits(65536,cb)]=e4m3fn(raw[:65536]);return value
decode(root/'contract.fp8').tofile(out/'input.f32')
np.stack([decode(root/f'qkv-{i}.fp8',i) for i in range(3)]).tofile(out/'oracle.f32')
matrices,scales=unpack(base/'block31-qkv.weights');np.concatenate([*[m.ravel() for m in matrices],scales]).astype('<f4').tofile(out/'weights.f32')
attention_out=base/'amd-qkv-attention';attention_out.mkdir(exist_ok=True)
for name in ['input.f32','weights.f32']:(attention_out/name).write_bytes((out/name).read_bytes())
decode(root/'attention.fp8').tofile(attention_out/'oracle.f32')
