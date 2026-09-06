"""Identity control for native split projection/pool layout and arithmetic."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512');folder=root/'pool-check';folder.mkdir(exist_ok=True)
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
source=root/'plain-continuation/input.fp8';packed=np.fromfile(source,np.uint8)
x=e4m3fn(packed.reshape(-1,8192)[:,inverse]).reshape(2,4,4,4,512).transpose(0,2,1,3,4).reshape(8,16,512)
weights=np.zeros(263168,np.uint8);weights[262144:].view('<f2')[:]=1;weights.tofile(folder/'identity.weights')
np.zeros_like(packed).tofile(folder/'zero.fp8')
subprocess.run(['/tmp/native-split-pool-oracle',str(folder/'zero.fp8'),str(source),str(folder/'identity.weights'),str(folder/'main.fp8'),str(folder/'pool.fp8'),'16','8'],check=True)
raw=np.fromfile(folder/'main.fp8',np.uint8);assert not np.any(raw[65536:])
main=e4m3fn(raw[:65536].reshape(-1,8192)[:,inverse]).reshape(2,4,4,4,512).transpose(0,2,1,3,4).reshape(x.shape)
print(json.dumps({'main_different':int(np.count_nonzero(main!=x))}),flush=True)
assert np.array_equal(main,x),'main contract differs'
raw=np.fromfile(folder/'pool.fp8',np.uint8);print(json.dumps({'pool_nonzero':int(np.count_nonzero(raw)),'pool_last_nonzero':int(np.flatnonzero(raw)[-1]) if np.any(raw) else None}),flush=True)
assert not np.any(raw[16384:]) and not np.any((raw[:16384]&127)==127)
top=H(x[::2,::2]+x[::2,1::2]);bottom=H(x[1::2,::2]+x[1::2,1::2]);want=F(H(H(top+bottom)*.25))
bank=e4m3fn(raw[:16384]).reshape(32,4,8,16).transpose(1,2,0,3).reshape(4,8,512)
cell=e4m3fn(raw[:16384].reshape(-1,8192)[:,inverse]).reshape(1,2,4,4,512).transpose(0,2,1,3,4).reshape(want.shape)
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
for name,got in [('bank',bank),('bank-swap',bank[...,perm]),('cell',cell)]:
 print(json.dumps({'pool_layout':name,'different':int(np.count_nonzero(got!=want)),'max_error':float(np.max(np.abs(got-want)))}),flush=True)
assert np.array_equal(cell,want),'pool cell layout/arithmetic differs'
