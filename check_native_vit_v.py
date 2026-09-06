"""Verify V alone against original QKV, with measured matrix/output coordinates."""
from pathlib import Path
import argparse,subprocess,json
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--folder',type=Path,default=Path('release/native-vit'));args=parser.parse_args()
root=args.folder;weight_path=Path('release/native-vit/block31-qkv.weights')
subprocess.run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(weight_path),str(root/'qkv-vcheck'),'4','4','16'],check=True)
source=np.fromfile(root/'contract.fp8',np.uint8);assert not np.any(source[16384:])
x=np.empty((16,1024),np.float32);x[bits(16384,[2,6,7,8]),bits(16384,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(source[:16384])
raw=np.fromfile(weight_path,np.uint8);assert raw.size==3145856
vweights=raw[128:].reshape(-1,3,1024)[:,2,:].ravel()
matrix=np.empty((1024,1024),np.float32);matrix[bits(1048576,[6,3,9,7,8,10,11,12,13,14]),bits(1048576,[0,1,2,4,5,15,16,17,18,19])]=e4m3fn(vweights)
predicted=F(H(multiply(x[:,:512],matrix[:,:512])+multiply(x[:,512:],matrix[:,512:])))
v=np.fromfile(root/'qkv-vcheck-2.fp8',np.uint8);assert not np.any(v[32768:]) and not np.any((v[:32768]&127)==127)
valid=np.flatnonzero((np.arange(32768)&4)==0);assert not np.any(v[np.flatnonzero((np.arange(32768)&4)!=0)])
t=bits(32768,[1,0,4,5])[valid];c=bits(32768,[6,3,9,7,8,10,11,12,13,14])[valid]
assert np.unique(t*1024+c).size==16384
target=np.empty_like(predicted);target[t,c]=e4m3fn(v[valid]);error=np.abs(predicted-target)
print(json.dumps({'fixture':str(root),'V_values':16384,'different':int(np.count_nonzero(error)),'max_error':float(error.max()),'full_QKV_verified':False}),flush=True)
assert np.array_equal(predicted,target),'native V arithmetic or coordinates differ'
