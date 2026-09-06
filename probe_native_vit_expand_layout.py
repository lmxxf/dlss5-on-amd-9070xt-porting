"""Recover address-bit effects with unit coefficients and coded source addresses."""
from pathlib import Path
import os,subprocess,json
import numpy as np
from native_c32_reference import H
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
root=Path('release/native-vit');env=os.environ.copy();env['DLSS5_VIT_EXPAND_LAYOUT_SCAN']='1'
subprocess.run(['/tmp/native-vit-expand-oracle',str(root/'input.fp8'),str(root/'layout-scan.fp8'),str(root/'block31-expand.weights'),'16','32'],check=True,env=env)
raw=np.fromfile(root/'layout-scan.fp8',np.uint8).reshape(23,4,65536)
alphabet=e4m3fn(np.arange(0x20,0x30,dtype=np.uint8));gate=np.clip(alphabet,-4,4)
encoded=quantize(H(alphabet*H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))))
assert np.unique(encoded).size==16
decode=np.full(256,-1,np.int32);decode[encoded]=np.arange(16)
positions=[];sources=[]
for probe in range(23):
 active=np.flatnonzero(raw[probe,0]);assert active.size==16,(probe,active.size)
 assert all(np.array_equal(np.flatnonzero(raw[probe,p]),active) for p in range(4))
 code=raw[probe,:,active].T if raw[probe,:,active].shape==(16,4) else raw[probe,:,active]
 assert code.shape==(4,16) and np.all(decode[code]>=0)
 address=np.zeros(16,np.uint32)
 for part in range(4):address|=decode[code[part]].astype(np.uint32)<<(4*part)
 positions.append(active);sources.append(address)
 print(json.dumps({'weight_bit':probe-1,'output_xor':np.unique(active^positions[0]).tolist(),'input_xor':np.unique(address^sources[0]).tolist(),'input_min':int(address.min()),'input_max':int(address.max())}),flush=True)
np.savez(root/'expand-address-probes.npz',positions=np.array(positions),sources=np.array(sources))
