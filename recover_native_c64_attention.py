"""Locate C64/C128 value operands independently within the packed QKV region."""
from pathlib import Path
import os,subprocess,json,sys,argparse
import numpy as np
parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=64);parser.add_argument('--full',action='store_true');args=parser.parse_args()
C=args.channels;heads=C//32;N=C*C
size,fs,scale,qkv_begin,projection_begin=(61760,0x7010,0xe0a0,0x70a0,0xe0b0) if C==64 else (197184,0x18010,0x2c120,0x18120,0x2c130)
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if C==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
symbol=f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_inpview_fp8'
folder=Path(f'release/native-c{C}/attention-layout');folder.mkdir(parents=True,exist_ok=True)
w=np.zeros(size,np.uint8)
w.view('<f2')[fs//2:fs//2+C]=1
w.view('<f4')[scale//4:scale//4+heads]=1
w[projection_begin:projection_begin+N]=0x38
w.tofile(folder/'locate-v.weights')
np.full(64*C,0x38,np.uint8).tofile(folder/'ones.fp8')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
env.update(DLSS5_NATIVE_SCAN_OFFSET=str(qkv_begin),DLSS5_NATIVE_SCAN_COUNT=str(3*N))
subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'locate-v.weights'),str(folder/'ones.fp8'),str(folder/'locate-v.fp8'),str(folder/'unused.fp8'),symbol,'8','8','1','1',str(heads),'7','0'],env=env,check=True)
raw=np.fromfile(folder/'locate-v.fp8',np.uint8).reshape(3*N,64*C)
assert not np.any((raw&127)==127)
active=np.flatnonzero(np.any((raw&127)!=0,axis=1))
assert active.size==N
cuts=np.flatnonzero(np.diff(active)!=1)+1
ranges=[[int(part[0]+qkv_begin),int(part[-1]+qkv_begin+1)] for part in np.split(active,cuts)]
np.savez(folder/'v-slots.npz',offsets=active+qkv_begin)
print(json.dumps({'value_byte_count':int(active.size),'value_ranges_hex':[[hex(a),hex(b)] for a,b in ranges],'nonzero_codes':np.unique(raw[raw!=0]).tolist(),'method':'Q/K-only probes cannot emit values when V=0; output projection is all ones'},indent=2))
if not args.full:sys.exit(0)
v_offsets=active+qkv_begin
inverse=np.argsort(np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc'])
def control():
 result=np.zeros(size,np.uint8);result.view('<f2')[fs//2:fs//2+C]=1
 result.view('<f4')[scale//4:scale//4+heads]=1
 return result
def scan(tag,weights,projection=False,selected=None):
 weights.tofile(folder/'scan.weights')
 ids=np.arange(C);physical=(ids&~3)|((ids&1)<<1)|((ids&2)>>1)
 chosen=np.ones(C,bool) if selected is None else np.isin(ids,selected)
 source=np.broadcast_to(np.where(chosen[physical],0x38,0).astype(np.uint8).reshape(C//16,1,1,16),(C//16,8,8,16)).copy()
 source.tofile(folder/'scan-input.fp8')
 environment=env.copy();environment.update(DLSS5_NATIVE_SCAN_OFFSET=str(projection_begin) if projection else str(qkv_begin),DLSS5_NATIVE_SCAN_COUNT=str(N) if projection else str(3*N))
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'scan.weights'),str(folder/'scan-input.fp8'),str(folder/'scan-output.fp8'),str(folder/'unused.fp8'),symbol,'8','8','1','1',str(heads),'7','0'],env=environment,check=True,capture_output=True)
 values=np.fromfile(folder/'scan-output.fp8',np.uint8).reshape(-1,64*C)
 if not projection:values=values[active]
 assert not np.any((values&127)==127)
 present=np.any((values.reshape(N,4,16*C)[:,:,inverse].reshape(N,64,C)&127)!=0,axis=1)
 print(tag,int(np.any(present,axis=1).sum()),flush=True)
 return present
v_input=np.zeros(N,np.int32)
for bit in range(C.bit_length()-1):
 weights=control();weights[projection_begin:projection_begin+N]=0x38
 present=scan(f'v-input-{bit}',weights,selected=np.flatnonzero(np.arange(C)&(1<<bit)))
 v_input|=np.any(present,axis=1).astype(np.int32)<<bit
assert np.all(np.bincount(v_input,minlength=C)==C)
representatives=np.flatnonzero(v_input==0);assert len(representatives)==C
p_input=np.zeros(N,np.int32);p_output=None
for bit in [-1,*range(C.bit_length()-1)]:
 weights=control();chosen=np.ones(C,bool) if bit<0 else (np.arange(C)&(1<<bit))!=0
 weights[v_offsets[representatives[chosen]]]=0x38
 present=scan(f'p-input-{bit}',weights,True,[0])
 if bit<0:
  assert np.all(present.sum(1)==1);p_output=np.argmax(present,axis=1)
 else:p_input|=np.any(present,axis=1).astype(np.int32)<<bit
assert np.unique(p_output*C+p_input).size==N
v_output=np.zeros(N,np.int32)
for bit in range(C.bit_length()-1):
 weights=control();weights[projection_begin+np.flatnonzero((p_output==0)&((p_input&(1<<bit))!=0))]=0x38
 present=scan(f'v-output-{bit}',weights)
 v_output|=np.any(present,axis=1).astype(np.int32)<<bit
assert np.unique(v_output*C+v_input).size==N
np.testing.assert_array_equal(v_output[representatives],np.arange(C))
np.savez(folder/'matrix-layout.npz',v_offsets=v_offsets,v_input=v_input,v_output=v_output,p_input=p_input,p_output=p_output)
print(json.dumps({'V_connections':N,'P_connections':N,'latent_width':C,'method':'binary-coded connectivity; latent labels shared across V and P'}))
