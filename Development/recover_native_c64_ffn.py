"""C64/C128 FFN byte connectivity recovery; exact probe maps, no weight fitting."""
from pathlib import Path
import os,subprocess,json,sys,argparse
import numpy as np

parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=64);parser.add_argument('--full',action='store_true');args=parser.parse_args()
C=args.channels;heads=C//32;hidden_count=4*C
w1_count=hidden_count*C;w2_count=C*128;w3_count=C*C;b2=w1_count;b3=b2+w2_count;end3=b3+w3_count
size,ats,scale=(61760,0xf0b0,0xe0a0) if C==64 else (197184,0x30130,0x2c120)
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if C==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
symbol=f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_inpview_fp8'
folder=Path(f'release/native-c{C}/ffn-layout');folder.mkdir(parents=True,exist_ok=True)
view=np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc']
inverse=np.argsort(view)
weights=np.zeros(size,np.uint8)
weights.view('<f2')[ats//2:ats//2+C]=1
weights.view('<f4')[scale//4:scale//4+heads]=1
# First matrix: C terms of 1/C produce exactly 1. The grouped second
# matrix has positive operands, so every reachable intermediate is active.
weights[:b2]=0x08 if C==64 else 0x04
weights[b2:b3]=0x38
weights.tofile(folder/'w3-row.weights')
np.full(64*C,0x38,np.uint8).tofile(folder/'ones.fp8')
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
environment.update(DLSS5_NATIVE_SCAN_OFFSET=str(b3),DLSS5_NATIVE_SCAN_COUNT=str(w3_count))
subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'w3-row.weights'),str(folder/'ones.fp8'),str(folder/'w3-row.fp8'),str(folder/'unused-aux.fp8'),symbol,'8','8','1','1',str(heads),'7','0'],env=environment,check=True)
raw=np.fromfile(folder/'w3-row.fp8',np.uint8).reshape(w3_count,4,16*C)[:,:,inverse].reshape(w3_count,64,C)
assert not np.any((raw&127)==127)
present=np.any((raw&127)!=0,axis=1)
assert np.all(present.sum(1)==1), 'each W3 byte must address one output channel'
rows=np.argmax(present,axis=1)
assert np.all(np.count_nonzero(raw&127,axis=(1,2))==64), 'probe must affect all 64 pixels'
assert np.all(np.bincount(rows,minlength=C)==C), 'W3 row capacities differ from CxC'
np.savez(folder/'w3-output.npz',output=rows)
print(json.dumps({'w3_bytes':w3_count,'output_channels':C,'bytes_per_output':C,'input_mapping':'pending coded probes','nonzero_codes':np.unique(raw[raw!=0]).tolist()},indent=2))
if not args.full:sys.exit(0)

def control():
 w=np.zeros(size,np.uint8);w.view('<f2')[ats//2:ats//2+C]=1
 w.view('<f4')[scale//4:scale//4+heads]=1
 return w
def scan(tag,w,offset,count,selected=None):
 w.tofile(folder/'scan.weights')
 ids=np.arange(C);physical=(ids&~3)|((ids&1)<<1)|((ids&2)>>1)
 active=np.ones(C,bool) if selected is None else np.isin(ids,selected)
 source=np.broadcast_to(np.where(active[physical],0x38,0).astype(np.uint8).reshape(C//16,1,1,16),(C//16,8,8,16)).copy()
 source.tofile(folder/'scan-input.fp8')
 env=environment.copy();env.update(DLSS5_NATIVE_SCAN_OFFSET=str(offset),DLSS5_NATIVE_SCAN_COUNT=str(count))
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'scan.weights'),str(folder/'scan-input.fp8'),str(folder/'scan-output.fp8'),str(folder/'unused-aux.fp8'),symbol,'8','8','1','1',str(heads),'7','0'],env=env,check=True,capture_output=True)
 values=np.fromfile(folder/'scan-output.fp8',np.uint8).reshape(count,64*C)
 assert not np.any((values&127)==127)
 response=np.any((values&127)!=0,axis=1)
 print(tag,int(response.sum()),flush=True)
 return response.astype(np.int32)

w1_input=np.zeros(w1_count,np.int32)
for bit in range(C.bit_length()-1):
 w=control();w[b2:end3]=0x38
 w1_input|=scan(f'w1-input-{bit}',w,0,w1_count,np.flatnonzero(np.arange(C)&(1<<bit)))<<bit
assert np.all(np.bincount(w1_input,minlength=C)==hidden_count)
representatives=np.flatnonzero(w1_input==0);assert len(representatives)==hidden_count
w2_hidden=np.zeros(w2_count,np.int32)
for bit in range(hidden_count.bit_length()-1):
 w=control();w[representatives[(np.arange(hidden_count)&(1<<bit))!=0]]=0x38;w[b3:end3]=0x38
 w2_hidden|=scan(f'w2-hidden-{bit}',w,b2,w2_count,[0])<<bit
assert np.all(np.bincount(w2_hidden,minlength=hidden_count)==32)
middle_representatives=np.flatnonzero(np.isin(w2_hidden,np.arange(0,hidden_count,128)));assert len(middle_representatives)==C
w3_input=np.zeros(w3_count,np.int32)
for bit in range(C.bit_length()-1):
 w=control();w[representatives[np.arange(0,hidden_count,128)]]=0x38
 w[b2+middle_representatives[(np.arange(C)&(1<<bit))!=0]]=0x38
 w3_input|=scan(f'w3-input-{bit}',w,b3,w3_count,[0])<<bit
assert np.unique(rows*C+w3_input).size==w3_count
w2_output=np.zeros(w2_count,np.int32)
for bit in range(C.bit_length()-1):
 w=control();w[representatives]=0x38
 w[b3+np.flatnonzero((rows==0)&((w3_input&(1<<bit))!=0))]=0x38
 w2_output|=scan(f'w2-output-{bit}',w,b2,w2_count,[0])<<bit
assert np.unique(w2_output*hidden_count+w2_hidden).size==w2_count
assert np.all(np.bincount(w2_output,minlength=C)==128)
w1_hidden=np.zeros(w1_count,np.int32)
for bit in range(hidden_count.bit_length()-1):
 w=control();w[b2+np.flatnonzero((w2_hidden&(1<<bit))!=0)]=0x38;w[b3:end3]=0x38
 w1_hidden|=scan(f'w1-hidden-{bit}',w,0,w1_count)<<bit
assert np.unique(w1_hidden*C+w1_input).size==w1_count
np.testing.assert_array_equal(w1_hidden[representatives],np.arange(hidden_count))
np.savez(folder/'layout.npz',w1_input=w1_input,w1_hidden=w1_hidden,w2_hidden=w2_hidden,w2_output=w2_output,w3_input=w3_input,w3_output=rows)
print(json.dumps({'connections':[w1_count,w2_count,w3_count],'hidden_width':hidden_count,'middle_width':C,'w2_inputs_per_output':128,'method':'binary-coded exact connectivity, no coefficient fitting'}))
