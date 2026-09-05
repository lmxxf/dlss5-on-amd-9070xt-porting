"""Locate C64 value operands independently within the packed QKV region."""
from pathlib import Path
import os,subprocess,json,sys
import numpy as np
folder=Path('release/native-c64/attention-layout');folder.mkdir(parents=True,exist_ok=True)
w=np.zeros(61760,np.uint8)
w.view('<f2')[0x7010//2:0x7090//2]=1
w.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
w[0xe0b0:0xf0b0]=0x38
w.tofile(folder/'locate-v.weights')
np.full(4096,0x38,np.uint8).tofile(folder/'ones.fp8')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
env.update(DLSS5_NATIVE_SCAN_OFFSET='0x70a0',DLSS5_NATIVE_SCAN_COUNT='12288')
subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'locate-v.weights'),str(folder/'ones.fp8'),str(folder/'locate-v.fp8'),str(folder/'unused.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','8','8','1','1','2','7','0'],env=env,check=True)
raw=np.fromfile(folder/'locate-v.fp8',np.uint8).reshape(12288,4096)
assert not np.any((raw&127)==127)
active=np.flatnonzero(np.any((raw&127)!=0,axis=1))
assert active.size==4096
cuts=np.flatnonzero(np.diff(active)!=1)+1
ranges=[[int(part[0]+0x70a0),int(part[-1]+0x70a0+1)] for part in np.split(active,cuts)]
np.savez(folder/'v-slots.npz',offsets=active+0x70a0)
print(json.dumps({'value_byte_count':int(active.size),'value_ranges_hex':[[hex(a),hex(b)] for a,b in ranges],'nonzero_codes':np.unique(raw[raw!=0]).tolist(),'method':'Q/K-only probes cannot emit values when V=0; output projection is all ones'},indent=2))
if '--full' not in sys.argv:sys.exit(0)
v_offsets=active+0x70a0
inverse=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
def control():
 result=np.zeros(61760,np.uint8);result.view('<f2')[0x7010//2:0x7090//2]=1
 result.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
 return result
def scan(tag,weights,projection=False,selected=None):
 weights.tofile(folder/'scan.weights')
 channels=np.arange(64);physical=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 chosen=np.ones(64,bool) if selected is None else np.isin(channels,selected)
 source=np.broadcast_to(np.where(chosen[physical],0x38,0).astype(np.uint8).reshape(4,1,1,16),(4,8,8,16)).copy()
 source.tofile(folder/'scan-input.fp8')
 environment=env.copy();environment.update(DLSS5_NATIVE_SCAN_OFFSET='0xe0b0' if projection else '0x70a0',DLSS5_NATIVE_SCAN_COUNT='4096' if projection else '12288')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'scan.weights'),str(folder/'scan-input.fp8'),str(folder/'scan-output.fp8'),str(folder/'unused.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','8','8','1','1','2','7','0'],env=environment,check=True,capture_output=True)
 values=np.fromfile(folder/'scan-output.fp8',np.uint8).reshape(-1,4096)
 if not projection:values=values[active]
 assert not np.any((values&127)==127)
 present=np.any((values.reshape(4096,4,1024)[:,:,inverse].reshape(4096,64,64)&127)!=0,axis=1)
 print(tag,int(np.any(present,axis=1).sum()),flush=True)
 return present
v_input=np.zeros(4096,np.int32)
for bit in range(6):
 weights=control();weights[0xe0b0:0xf0b0]=0x38
 present=scan(f'v-input-{bit}',weights,selected=np.flatnonzero(np.arange(64)&(1<<bit)))
 v_input|=np.any(present,axis=1).astype(np.int32)<<bit
assert np.all(np.bincount(v_input,minlength=64)==64)
representatives=np.flatnonzero(v_input==0);assert len(representatives)==64
p_input=np.zeros(4096,np.int32);p_output=None
for bit in [-1,0,1,2,3,4,5]:
 weights=control();chosen=np.ones(64,bool) if bit<0 else (np.arange(64)&(1<<bit))!=0
 weights[v_offsets[representatives[chosen]]]=0x38
 present=scan(f'p-input-{bit}',weights,True,[0])
 if bit<0:
  assert np.all(present.sum(1)==1);p_output=np.argmax(present,axis=1)
 else:p_input|=np.any(present,axis=1).astype(np.int32)<<bit
assert np.unique(p_output*64+p_input).size==4096
v_output=np.zeros(4096,np.int32)
for bit in range(6):
 weights=control();weights[0xe0b0+np.flatnonzero((p_output==0)&((p_input&(1<<bit))!=0))]=0x38
 present=scan(f'v-output-{bit}',weights)
 v_output|=np.any(present,axis=1).astype(np.int32)<<bit
assert np.unique(v_output*64+v_input).size==4096
np.testing.assert_array_equal(v_output[representatives],np.arange(64))
np.savez(folder/'matrix-layout.npz',v_offsets=v_offsets,v_input=v_input,v_output=v_output,p_input=p_input,p_output=p_output)
print(json.dumps({'V_connections':4096,'P_connections':4096,'latent_width':64,'method':'binary-coded connectivity; latent labels shared across V and P'}))
