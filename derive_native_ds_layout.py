"""Extend DS address-bit rules only after exact checks on measured smaller maps."""
from pathlib import Path
import json
import numpy as np
def maps(C):
 d=C.bit_length()-1;assert C in (64,128,256);n=2*C*C;index=np.arange(n,dtype=np.int32)
 def gather(positions):
  result=np.zeros(n,np.int32)
  for bit,position in enumerate(positions):result|=((index>>position)&1)<<bit
  return result
 result={'input':gather([1,0,4,5,2]+list(range(d+6,2*d+1))),'output':gather([3,6,7,8,9]+list(range(10,d+6)))}
 assert np.unique(result['output']*C+result['input']).size==n
 return result
for C in (64,128):
 measured=np.load(f'release/native-c{C}/ds-layout/layout.npz')
 for key,value in maps(C).items():np.testing.assert_array_equal(value,measured[key])
folder=Path('release/native-c256/ds-layout');folder.mkdir(parents=True,exist_ok=True)
np.savez(folder/'layout.npz',**maps(256))
print(json.dumps({'C64_C128_measured_DS_maps':'all exact','C256_DS_connections':131072,'C256_arithmetic_validation':'required'}))
