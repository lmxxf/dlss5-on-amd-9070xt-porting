"""Recover V source/output address effects using unit matrix coefficients."""
from pathlib import Path
import os,subprocess,json
import numpy as np
root=Path('release/native-vit');env=os.environ.copy();env['DLSS5_VIT_QKV_V_LAYOUT_SCAN']='1'
subprocess.run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(root/'block31-qkv.weights'),str(root/'qkv'),'4','4','16'],env=env,check=True)
raw=np.fromfile(root/'qkv-v-layout.fp8',np.uint8).reshape(21,4,32768)
positions=[];sources=[]
for probe in range(21):
 active=np.flatnonzero(raw[probe,0]);assert active.size==16
 assert all(np.array_equal(np.flatnonzero(raw[probe,p]),active) for p in range(4))
 code=raw[probe][:,active];assert np.all((code>=0x20)&(code<=0x2f))
 address=np.zeros(16,np.uint32)
 for part in range(4):address|=(code[part].astype(np.uint32)-0x20)<<(4*part)
 positions.append(active);sources.append(address)
 print(json.dumps({'V_matrix_bit':probe-1,'output_xor':np.unique(active^positions[0]).tolist(),'input_xor':np.unique(address^sources[0]).tolist()}),flush=True)
np.savez(root/'v-address-probes.npz',positions=np.array(positions),sources=np.array(sources))
