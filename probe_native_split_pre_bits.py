"""Measure which address bits select input channels of split ffwd's first matrix."""
from pathlib import Path
import os,json,subprocess
import numpy as np
root=Path('release/native-c512/pre-bit-probe');root.mkdir(parents=True,exist_ok=True)
control=Path('release/native-c512/gate-probe')
env=os.environ.copy();env['DLSS5_SPLIT_FFWD_ONLY']='1'
channels=np.arange(512);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
def run(offset,bit=None):
 w=np.zeros(524288,np.uint8);w[[offset,0x40000,0x60000]]=0x38;w.tofile(root/'weights.bin')
 chosen=np.ones(512,bool) if bit is None else (channels&(1<<bit))!=0
 values=np.broadcast_to(np.where(chosen[perm],0x38,0).astype(np.uint8).reshape(32,1,1,16),(32,8,8,16)).copy()
 values.tofile(root/'input.fp8');out=root/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(root/'input.fp8'),str(out),str(root/'weights.bin'),*[str(control/f'w{i}.bin') for i in range(1,4)],'8','8','0','native-inpview'],env=env,check=True,capture_output=True)
 raw=np.fromfile(out,np.uint8);assert raw.size==32768 and np.all((raw==0)|(raw==0x3a))
 n=np.count_nonzero(raw);assert n in (0,64)
 return n!=0
assert run(0)
active=[bit for bit in range(18) if run(1<<bit)]
mapping={}
for address in [None,*active]:
 value=0
 for bit in range(9):value|=int(run(0 if address is None else 1<<address,bit))<<bit
 mapping['base' if address is None else str(address)]=value
print(json.dumps({'active_address_bits':active,'input_coordinates':mapping},indent=2))
assert len(active)==9 and mapping['base']==0
assert sorted(mapping[str(bit)] for bit in active)==[1<<bit for bit in range(9)]
np.savez(root/'input-bits.npz',address_bits=np.array(active),input_coordinates=np.array([mapping[str(bit)] for bit in active]))
