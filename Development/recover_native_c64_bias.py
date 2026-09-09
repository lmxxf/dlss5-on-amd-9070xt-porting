"""Recover C64/C128 attention bias coordinates through spatial binary probes."""
from pathlib import Path
import os,subprocess,json,argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=64);args=parser.parse_args()
C=args.channels;NH=C//32;count=NH*4096
size,fs,scale,p_begin,bias_begin=(61760,0x7010,0xe0a0,0xe0b0,0xa0a0) if C==64 else (197184,0x18010,0x2c120,0x2c130,0x24120)
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if C==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
folder=Path(f'release/native-c{C}/attention-layout')
layout=np.load(folder/'matrix-layout.npz')
inverse=np.argsort(np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc'])
w=np.zeros(size,np.uint8);w.view('<f2')[fs//2:fs//2+C]=1
w.view('<f4')[scale//4:scale//4+NH]=1
w[layout['v_offsets'][layout['v_input']==0]]=0x38
for out,latent in [(head,head*32) for head in range(NH)]:
 slots=np.flatnonzero((layout['p_output']==out)&(layout['p_input']==latent));assert slots.size==1
 w[p_begin+slots]=0x38
w.tofile(folder/'bias-control.weights')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
env.update(DLSS5_NATIVE_SCAN_OFFSET=str(bias_begin),DLSS5_NATIVE_SCAN_COUNT=str(count),DLSS5_NATIVE_SCAN_HALF='1')
keys=np.zeros(count,np.int32);queries=None;heads=None
for bit in range(6):
 source=np.zeros((C//16,8,8,16),np.uint8);source[0,:,:,0]=np.where(np.arange(64).reshape(8,8)&(1<<bit),0x38,0)
 source.tofile(folder/'bias-input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'bias-control.weights'),str(folder/'bias-input.fp8'),str(folder/'bias-output.fp8'),str(folder/'unused.fp8'),f'cc_tinlayout_fused_swin_{NH}h_{C}_{NH}_inpview_fp8','8','8','1','1',str(NH),'7','0'],env=env,check=True,capture_output=True)
 raw=np.fromfile(folder/'bias-output.fp8',np.uint8).reshape(count,4,16*C)[:,:,inverse].reshape(count,2,2,4,4,C).transpose(0,1,3,2,4,5).reshape(count,64,C)
 assert not np.any(raw[:,:,NH:])
 difference=e4m3fn(raw[:,:,:NH])-.5
 changed=np.abs(difference)>.1
 assert np.all(changed.sum((1,2))==1), 'each bias must change one query in one head'
 index=np.argmax(changed.reshape(count,64*NH),axis=1);q=index//NH;head=index%NH
 if queries is None:queries=q;heads=head
 else:np.testing.assert_array_equal(queries,q);np.testing.assert_array_equal(heads,head)
 keys|=(difference[np.arange(count),q,head]>0).astype(np.int32)<<bit
 print('bias bit',bit,'heads',np.bincount(head).tolist(),flush=True)
assert np.unique(heads*4096+queries*64+keys).size==count
np.savez(folder/'bias-layout.npz',head=heads,query=queries,key=keys)
print(json.dumps({'bias_entries':count,'heads':NH,'queries_per_head':64,'keys_per_query':64,'bijection':True}))
