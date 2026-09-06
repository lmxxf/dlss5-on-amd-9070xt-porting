"""Original pool/head continuation from same-RGB block30 intermediates."""
from pathlib import Path
import json,subprocess
import numpy as np
from native_c32_reference import H,F
from native_c64_reference import multiply
from native_split_weights import unpack
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
base=Path('release/native-rgb-valid1080/encoder-split/decoder-block30');root=base.parent/'pool-head';root.mkdir(exist_ok=False)
r=json.loads((base/'validation.json').read_text());assert r['status']=='pass' and all(c['different']==0 for c in r['checks'])
subprocess.run(['/tmp/native-split-pool-oracle',str(base/'output.fp8.attn'),str(base/'output.fp8.ffn'),str(base/'block30-3.weights'),str(root/'main.fp8'),str(root/'pool.fp8'),'60','36','32','20'],check=True,timeout=20)
inv=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
def decode(path,h,w):
 raw=np.fromfile(path,np.uint8);n=h*w*512;assert not raw[n:].any() and not np.any((raw[:n]&127)==127)
 return e4m3fn(raw[:n].reshape(-1,8192)[:,inv]).reshape(h//4,w//4,4,4,512).transpose(0,2,1,3,4).reshape(h,w,512)
projection=unpack(base,30)[-1];raw=multiply(decode(base/'output.fp8.attn',36,60),projection['matrix'],H(decode(base/'output.fp8.ffn',36,60)*projection['skip']))
assert np.array_equal(F(raw),decode(root/'main.fp8',36,60))
pooled=np.zeros((20,32,512),np.float32);pooled[:18,:30]=F(H(H(H(raw[::2,::2]+raw[::2,1::2])+H(raw[1::2,::2]+raw[1::2,1::2]))*.25))
assert np.array_equal(pooled,decode(root/'pool.fp8',20,32))
weights=Path('release/native-c512/block30-4.weights')
subprocess.run(['/tmp/native-split-head-oracle',str(root/'pool.fp8'),str(root/'head.fp8'),str(weights),'32','20','16','3','1','8'],check=True,timeout=20)
w=np.fromfile(weights,np.uint8);matrix=np.empty((1024,512),np.float32);matrix[bits(524288,[3,6,7,8,9,10,11,12,13,14]),bits(524288,[1,0,4,5,2,15,16,17,18])]=e4m3fn(w[:524288])
v=np.fromfile(root/'head.fp8',np.uint8);n=640*1024;assert not v[n:].any()
head=e4m3fn(v[:n].reshape(-1,2,8192)[:,:,inv]).reshape(-1,2,4,4,512).transpose(0,2,3,1,4).reshape(5,8,4,4,1024).transpose(0,2,1,3,4).reshape(20,32,1024)
assert np.isfinite(head).all() and np.array_equal(F(multiply(pooled,matrix)),head)
head.tofile(root/'head.f32');matrix.tofile(root/'head-matrix.f32');raw.tofile(root/'block30-raw.f32')
(root/'validation.json').write_text(json.dumps({'scope':'same valid1080 RGB original pool/head continuation; not GPU full chain','main_exact':True,'pool_exact':True,'head_exact':True,'head_values':n},indent=2)+'\n');print('Original same-RGB pool/head: main, padded pool and head exact')
