"""Diagnose block8 raw pooling/projection against independently decoded main."""
from pathlib import Path
import json,os,subprocess
import numpy as np
from native_c64_reference import unpack,block,multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c64')
inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
def decode(name):
 raw=np.fromfile(root/name,np.uint8);assert not np.any(raw[32768:])
 return e4m3fn(raw[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(16,32,64)
x=decode('block7-output.fp8');padded=np.pad(x,((4,4),(0,0),(0,0)))
tiles=padded.reshape(3,8,4,8,64).transpose(0,2,1,3,4).reshape(12,64,64)
result=block(tiles,*unpack(root/'block8.weights'),raw_output=True).reshape(3,4,8,8,64).transpose(0,2,1,3,4).reshape(padded.shape)[4:20]
target_main=decode('block8-output.fp8')
assert np.array_equal(F(result),target_main), 'block8 main differs'
rows=H(result[:,::2]+result[:,1::2]);pooled=F(H(H(rows[::2]+rows[1::2])*.25))
layout=np.load(root/'ffn-layout/layout.npz');weights=np.fromfile(root/'block8.weights',np.uint8)[0xf130:]
matrix=np.empty((128,64),np.float32);matrix[layout['w1_hidden'][:8192],layout['w1_input'][:8192]]=e4m3fn(weights)
prediction=F(multiply(pooled,matrix));raw=np.fromfile(root/'block8-aux.fp8',np.uint8)[:16384]
reports={'main_exact':True}
channels=np.arange(128);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
for bank in (16,32,64,128):
 target=e4m3fn(raw).reshape(128//bank,8,16,bank).transpose(1,2,0,3).reshape(8,16,128)
 for order in (channels,perm):
  got=prediction[...,order];err=np.abs(got-target)
  reports[f'bank{bank}_swap{int(order is perm)}']={'exact_fraction':float(np.mean(got==target)),'mae':float(err.mean()),'max_error':float(err.max())}
print(json.dumps(reports,indent=2))
# Independent row codes establish the bank ordering rather than choosing a
# layout solely because it correlates with the real-weight output.
control=np.zeros(69936,np.uint8);control.view('<f2')[0x7010//2:0x7090//2]=1
control.view('<f2')[0xf0b0//2:0xf130//2]=1;control.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
codes=np.concatenate([np.arange(8,120),np.arange(136,152)]).astype(np.uint8)
slots=np.flatnonzero(layout['w1_input'][:8192]==0)
control[0xf130+slots]=codes[layout['w1_hidden'][slots]]
control.tofile(root/'ds128-coded.weights')
cell_map=np.load(root/'view/mapping.npz')['cell_output_to_hwc']
logical=np.zeros((4,4,64),np.uint8);logical[:,:,0]=0x38
np.tile(logical.ravel()[cell_map],4).tofile(root/'ds128-coded-input.fp8')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(root/'ds128-coded.weights'),str(root/'ds128-coded-input.fp8'),str(root/'ds128-coded-main.fp8'),str(root/'ds128-coded-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_ds_fp8','8','8','1','2','2','8','2'],env=env,check=True,capture_output=True)
observed=np.fromfile(root/'ds128-coded-aux.fp8',np.uint8);assert not np.any(observed[2048:])
observed=observed[:2048].reshape(8,4,4,16).transpose(1,2,0,3).reshape(4,4,128)
assert np.all(observed==codes[perm]), 'coded 128-channel projection ordering failed'
target=e4m3fn(raw).reshape(8,8,16,16).transpose(1,2,0,3).reshape(8,16,128)
assert np.array_equal(prediction[...,perm],target), 'block8 DS differs from original'
print('coded_order=exact real_block8_main_and_ds=exact')
