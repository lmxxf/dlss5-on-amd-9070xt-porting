"""Validate address-bit rules on measured C64/C128 maps, then extend to C256."""
from pathlib import Path
import json
import numpy as np

def bits(count,positions):
 source=np.arange(count,dtype=np.int32)
 result=np.zeros(count,np.int32)
 for bit,position in enumerate(positions):result|=((source>>position)&1)<<bit
 return result
def maps(C,base):
 d=C.bit_length()-1;N=C*C;i=np.arange(N)
 inputs=bits(N,[1,0,4,5,2]+list(range(d+5,2*d)))
 outputs=bits(N,[3,6,7,8,9]+list(range(10,d+5)))
 matrix=dict(v_offsets=base+(i//1024)*3072+2048+i%1024,v_input=inputs,v_output=outputs,p_input=inputs,p_output=outputs)
 count=(C//32)*4096
 bias=dict(head=bits(count,list(range(12,12+d-5))),query=bits(count,[5,6,10,7,1,11]),key=bits(count,[0,3,8,4,2,9]))
 assert np.unique(outputs*C+inputs).size==N
 assert np.unique(bias['head']*4096+bias['query']*64+bias['key']).size==count
 return matrix,bias
for C,base in [(64,0x70a0),(128,0x18120)]:
 for name,derived in zip(('matrix-layout','bias-layout'),maps(C,base)):
  measured=np.load(f'release/native-c{C}/attention-layout/{name}.npz')
  for key,value in derived.items():np.testing.assert_array_equal(measured[key],value)
folder=Path('release/native-c256/attention-layout');folder.mkdir(parents=True,exist_ok=True)
for name,derived in zip(('matrix-layout','bias-layout'),maps(256,0x58220)):
 np.savez(folder/f'{name}.npz',**derived)
print(json.dumps({'C64_C128_measured_maps':'all exact','C256_V_P_connections_each':65536,'C256_bias_entries':32768,'C256_arithmetic_validation':'required'}))
