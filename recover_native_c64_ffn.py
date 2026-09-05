"""C64 FFN byte connectivity recovery; save exact probe maps, never fit weights."""
from pathlib import Path
import os,subprocess,json,sys
import numpy as np

folder=Path('release/native-c64/ffn-layout');folder.mkdir(parents=True,exist_ok=True)
view=np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc']
inverse=np.argsort(view)
weights=np.zeros(61760,np.uint8)
weights.view('<f2')[0xf0b0//2:0xf130//2]=1
weights.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
# First matrix: 64 terms of 1/64 produce exactly 1. The grouped second
# matrix has positive operands, so every reachable intermediate is active.
weights[:0x4000]=0x08
weights[0x4000:0x6000]=0x38
weights.tofile(folder/'w3-row.weights')
np.full(4096,0x38,np.uint8).tofile(folder/'ones.fp8')
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
environment.update(DLSS5_NATIVE_SCAN_OFFSET='0x6000',DLSS5_NATIVE_SCAN_COUNT='4096')
subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'w3-row.weights'),str(folder/'ones.fp8'),str(folder/'w3-row.fp8'),str(folder/'unused-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','8','8','1','1','2','7','0'],env=environment,check=True)
raw=np.fromfile(folder/'w3-row.fp8',np.uint8).reshape(4096,4,1024)[:,:,inverse].reshape(4096,64,64)
assert not np.any((raw&127)==127)
present=np.any((raw&127)!=0,axis=1)
assert np.all(present.sum(1)==1), 'each W3 byte must address one output channel'
rows=np.argmax(present,axis=1)
assert np.all(np.count_nonzero(raw&127,axis=(1,2))==64), 'probe must affect all 64 pixels'
assert np.all(np.bincount(rows,minlength=64)==64), 'W3 row capacities differ from 64x64'
np.savez(folder/'w3-output.npz',output=rows)
print(json.dumps({'w3_bytes':4096,'output_channels':64,'bytes_per_output':64,'input_mapping':'pending coded probes','nonzero_codes':np.unique(raw[raw!=0]).tolist()},indent=2))
if '--full' not in sys.argv:sys.exit(0)

def control():
 w=np.zeros(61760,np.uint8);w.view('<f2')[0xf0b0//2:0xf130//2]=1
 w.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
 return w
def scan(tag,w,offset,count,selected=None):
 w.tofile(folder/'scan.weights')
 channels=np.arange(64);physical=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 active=np.ones(64,bool) if selected is None else np.isin(channels,selected)
 source=np.broadcast_to(np.where(active[physical],0x38,0).astype(np.uint8).reshape(4,1,1,16),(4,8,8,16)).copy()
 source.tofile(folder/'scan-input.fp8')
 env=environment.copy();env.update(DLSS5_NATIVE_SCAN_OFFSET=str(offset),DLSS5_NATIVE_SCAN_COUNT=str(count))
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'scan.weights'),str(folder/'scan-input.fp8'),str(folder/'scan-output.fp8'),str(folder/'unused-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','8','8','1','1','2','7','0'],env=env,check=True,capture_output=True)
 values=np.fromfile(folder/'scan-output.fp8',np.uint8).reshape(count,4096)
 assert not np.any((values&127)==127)
 response=np.any((values&127)!=0,axis=1)
 print(tag,int(response.sum()),flush=True)
 return response.astype(np.int32)

w1_input=np.zeros(16384,np.int32)
for bit in range(6):
 w=control();w[0x4000:0x7000]=0x38
 w1_input|=scan(f'w1-input-{bit}',w,0,16384,np.flatnonzero(np.arange(64)&(1<<bit)))<<bit
assert np.all(np.bincount(w1_input,minlength=64)==256)
representatives=np.flatnonzero(w1_input==0);assert len(representatives)==256
w2_hidden=np.zeros(8192,np.int32)
for bit in range(8):
 w=control();w[representatives[(np.arange(256)&(1<<bit))!=0]]=0x38;w[0x6000:0x7000]=0x38
 w2_hidden|=scan(f'w2-hidden-{bit}',w,0x4000,8192,[0])<<bit
assert np.all(np.bincount(w2_hidden,minlength=256)==32)
middle_representatives=np.flatnonzero(np.isin(w2_hidden,[0,128]));assert len(middle_representatives)==64
w3_input=np.zeros(4096,np.int32)
for bit in range(6):
 w=control();w[representatives[[0,128]]]=0x38
 w[0x4000+middle_representatives[(np.arange(64)&(1<<bit))!=0]]=0x38
 w3_input|=scan(f'w3-input-{bit}',w,0x6000,4096,[0])<<bit
assert np.unique(rows*64+w3_input).size==4096
w2_output=np.zeros(8192,np.int32)
for bit in range(6):
 w=control();w[representatives]=0x38
 w[0x6000+np.flatnonzero((rows==0)&((w3_input&(1<<bit))!=0))]=0x38
 w2_output|=scan(f'w2-output-{bit}',w,0x4000,8192,[0])<<bit
assert np.unique(w2_output*256+w2_hidden).size==8192
assert np.all(np.bincount(w2_output,minlength=64)==128)
w1_hidden=np.zeros(16384,np.int32)
for bit in range(8):
 w=control();w[0x4000+np.flatnonzero((w2_hidden&(1<<bit))!=0)]=0x38;w[0x6000:0x7000]=0x38
 w1_hidden|=scan(f'w1-hidden-{bit}',w,0,16384)<<bit
assert np.unique(w1_hidden*64+w1_input).size==16384
np.testing.assert_array_equal(w1_hidden[representatives],np.arange(256))
np.savez(folder/'layout.npz',w1_input=w1_input,w1_hidden=w1_hidden,w2_hidden=w2_hidden,w2_output=w2_output,w3_input=w3_input,w3_output=rows)
print(json.dumps({'connections':[16384,8192,4096],'hidden_width':256,'middle_width':64,'w2_inputs_per_output':128,'method':'binary-coded exact connectivity, no coefficient fitting'}))
