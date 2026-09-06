"""Identity-residual control of captured 60x36 -> 32x20 pool geometry."""
from pathlib import Path
import json,subprocess,os
import numpy as np
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
root=Path('release/native-c512/pool-game-extent');root.mkdir(exist_ok=True)
inv=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
x=F(np.random.default_rng(3010).uniform(.125,1,(36,60,512)).astype(np.float32))
cells=quantize(x).reshape(9,4,15,4,512).transpose(0,2,1,3,4).reshape(-1,8192)
packed=np.empty_like(cells);packed[:,inv]=cells;packed.tofile(root/'input.fp8')
np.zeros_like(packed).tofile(root/'zero.fp8')
w=np.zeros(263168,np.uint8);w[262144:].view('<f2')[:]=1;w.tofile(root/'identity.weights')
subprocess.run(['/tmp/native-split-pool-oracle',str(root/'zero.fp8'),str(root/'input.fp8'),str(root/'identity.weights'),str(root/'main.fp8'),str(root/'pool.fp8'),'60','36','32','20'],check=True)
def decode(path,h,w):
 raw=np.fromfile(path,np.uint8);n=h*w*512
 assert (os.getenv('DLSS5_POOL_POISON_OUTPUT') or not raw[n:].any()) and not np.any((raw[:n]&127)==127)
 return e4m3fn(raw[:n].reshape(-1,8192)[:,inv]).reshape(h//4,w//4,4,4,512).transpose(0,2,1,3,4).reshape(h,w,512)
main=decode(root/'main.fp8',36,60);pool=decode(root/'pool.fp8',20,32)
want=F(H(H(H(x[::2,::2]+x[::2,1::2])+H(x[1::2,::2]+x[1::2,1::2]))*.25))
report={'scope':'identity residual pool control; real projection coefficients not tested','poison_output':bool(os.getenv('DLSS5_POOL_POISON_OUTPUT')),'main_different':int(np.count_nonzero(main!=x)),
 'valid_different':int(np.count_nonzero(pool[:18,:30]!=want)),
 'bottom_nonzero':int(np.count_nonzero(pool[18:])), 'right_nonzero':int(np.count_nonzero(pool[:18,30:]))}
print(json.dumps(report,indent=2));(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['main_different']==0 and report['valid_different']==0
assert report['bottom_nonzero']==0 and report['right_nonzero']==0
