"""Express recovered C64/C128 connectivity as address-bit maps, extend to C256.

The extension is a layout hypothesis until real C256 arithmetic is checked.
No numerical weight or activation coefficients are fitted.
"""
from pathlib import Path
import json
import numpy as np

def layout(C):
 d=C.bit_length()-1;assert C in (64,128,256) and C==1<<d
 def bits(n,positions):
  index=np.arange(n,dtype=np.int32);out=np.zeros(n,np.int32)
  for target,source in enumerate(positions):out|=((index>>source)&1)<<target
  return out
 group=list(range(12,d+7))
 result={
  'w1_input':bits(4*C*C,[1,0,4,5,2]+group),
  'w1_hidden':bits(4*C*C,[3,6,7,8,9,10,11]+list(range(d+7,2*d+2))),
  'w2_hidden':bits(128*C,[1,0,4,5,2,10,11]+group),
  'w2_output':bits(128*C,[3,6,7,8,9]+group),
  'w3_input':bits(C*C,[1,0,4,5,2]+list(range(d+5,2*d))),
  'w3_output':bits(C*C,[3,6,7,8,9]+list(range(10,d+5)))}
 for name,columns,count in [('w1',C,4*C*C),('w2',4*C,128*C),('w3',C,C*C)]:
  ro=result[name+'_hidden'] if name=='w1' else result[name+'_output']
  co=result[name+'_hidden'] if name=='w2' else result[name+'_input']
  assert np.unique(ro*columns+co).size==count
 return result

for C in (64,128):
 original=np.load(f'release/native-c{C}/ffn-layout/layout.npz')
 for key,value in layout(C).items():np.testing.assert_array_equal(value,original[key])
folder=Path('release/native-c256/ffn-layout');folder.mkdir(parents=True,exist_ok=True)
np.savez(folder/'layout.npz',**layout(256))
np.full(64*256,0x38,np.uint8).tofile(folder/'ones.fp8')
print(json.dumps({'C64_C128_all_six_maps_exact':True,'C256_connections':[262144,32768,65536],'C256_arithmetic_validation':'required'}))
