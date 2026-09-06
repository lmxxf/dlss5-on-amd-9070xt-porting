"""Identify input-bit positions of split ffwd's grouped expansion matrix."""
from pathlib import Path
import os,json,subprocess
import numpy as np
root=Path('release/native-c512/expand-bit-probe');root.mkdir(parents=True,exist_ok=True)
control=Path('release/native-c512/gate-probe');env=os.environ.copy();env['DLSS5_SPLIT_FFWD_ONLY']='1'
indices=np.arange(262144);input_bits=[1,0,4,5,2,14,15,16,17]
mask=sum(1<<b for b in input_bits);representatives=np.flatnonzero((indices&mask)==0);assert len(representatives)==512
np.full(32768,0x38,np.uint8).tofile(root/'input.fp8')
active=[]
for bit in range(17):
 w=np.zeros(524288,np.uint8);w[representatives]=0x38;w[0x40000+(1<<bit)]=0x38;w[0x60000]=0x38;w.tofile(root/'weights.bin')
 out=root/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(root/'input.fp8'),str(out),str(root/'weights.bin'),*[str(control/f'w{i}.bin') for i in range(1,4)],'8','8','0','native-inpview'],env=env,check=True,capture_output=True)
 raw=np.fromfile(out,np.uint8);assert raw.size==32768 and np.all((raw==0)|(raw==0x3a))
 n=np.count_nonzero(raw);assert n in (0,64)
 if n:active.append(bit)
print(json.dumps({'expansion_input_address_bits':active,'scope':'group0 hidden0 path'}))
assert active==[0,1,2,4,5,13]
