"""Initial ViT V matrix/storage candidates; histogram agreement is not acceptance."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-vit');raw=np.fromfile(root/'block31-qkv.weights',np.uint8)
source=np.fromfile(root/'contract.fp8',np.uint8);assert not np.any(source[16384:])
x=np.empty((16,1024),np.float32);x[bits(16384,[2,6,7,8]),bits(16384,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(source[:16384])
v=np.fromfile(root/'qkv-normal-2.fp8',np.uint8);assert not np.any(v[32768:]);v=v[:32768].reshape(-1,8);assert not np.any(v[:,4:])
target=e4m3fn(v[:,:4].ravel());assert not np.any((v[:,:4]&127)==127)
oi=bits(1048576,[6,3,9,7,8,10,11,12,13,14]);ii=bits(1048576,[0,1,2,4,5,15,16,17,18,19])
for chunk in [1048576,32768,2048,1024]:
 values=raw[:3145728].reshape(-1,3,chunk)[:,2,:].ravel()
 matrix=np.empty((1024,1024),np.float32);matrix[oi,ii]=e4m3fn(values)
 for reduction in ['serial','two_partitions']:
  result=multiply(x,matrix) if reduction=='serial' else H(multiply(x[:,:512],matrix[:,:512])+multiply(x[:,512:],matrix[:,512:]))
  result=F(result)
  print(json.dumps({'V_group_chunk':chunk,'reduction':reduction,'sorted_different':int(np.count_nonzero(np.sort(result.ravel())!=np.sort(target))),'candidate_range':[float(result.min()),float(result.max())],'original_range':[float(target.min()),float(target.max())]}),flush=True)
print(json.dumps({'acceptance':False,'reason':'output coordinates and full Q/K/V arithmetic not yet verified'}))
raise SystemExit(1)
