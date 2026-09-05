"""Recover both heads' bias coordinates through spatial binary probes."""
from pathlib import Path
import os,subprocess,json
import numpy as np
from decode_tinlayout_global import e4m3fn
folder=Path('release/native-c64/attention-layout')
layout=np.load(folder/'matrix-layout.npz')
inverse=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
w=np.zeros(61760,np.uint8);w.view('<f2')[0x7010//2:0x7090//2]=1
w.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
w[layout['v_offsets'][layout['v_input']==0]]=0x38
for out,latent in [(0,0),(1,32)]:
 slots=np.flatnonzero((layout['p_output']==out)&(layout['p_input']==latent));assert slots.size==1
 w[0xe0b0+slots]=0x38
w.tofile(folder/'bias-control.weights')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
env.update(DLSS5_NATIVE_SCAN_OFFSET='0xa0a0',DLSS5_NATIVE_SCAN_COUNT='8192',DLSS5_NATIVE_SCAN_HALF='1')
keys=np.zeros(8192,np.int32);queries=None;heads=None
for bit in range(6):
 source=np.zeros((4,8,8,16),np.uint8);source[0,:,:,0]=np.where(np.arange(64).reshape(8,8)&(1<<bit),0x38,0)
 source.tofile(folder/'bias-input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'bias-control.weights'),str(folder/'bias-input.fp8'),str(folder/'bias-output.fp8'),str(folder/'unused.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','8','8','1','1','2','7','0'],env=env,check=True,capture_output=True)
 raw=np.fromfile(folder/'bias-output.fp8',np.uint8).reshape(8192,4,1024)[:,:,inverse].reshape(8192,2,2,4,4,64).transpose(0,1,3,2,4,5).reshape(8192,64,64)
 assert not np.any(raw[:,:,2:])
 difference=e4m3fn(raw[:,:,:2])-.5
 changed=np.abs(difference)>.1
 assert np.all(changed.sum((1,2))==1), 'each bias must change one query in one head'
 index=np.argmax(changed.reshape(8192,128),axis=1);q=index//2;head=index%2
 if queries is None:queries=q;heads=head
 else:np.testing.assert_array_equal(queries,q);np.testing.assert_array_equal(heads,head)
 keys|=(difference[np.arange(8192),q,head]>0).astype(np.int32)<<bit
 print('bias bit',bit,'heads',np.bincount(head).tolist(),flush=True)
assert np.unique(heads*4096+queries*64+keys).size==8192
np.savez(folder/'bias-layout.npz',head=heads,query=queries,key=keys)
print(json.dumps({'bias_entries':8192,'heads':2,'queries_per_head':64,'keys_per_query':64,'bijection':True}))
