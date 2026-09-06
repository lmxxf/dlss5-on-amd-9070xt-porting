"""Original projection against four ordered K256 partitions plus residual."""
from pathlib import Path
import argparse,subprocess,json
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from unpack_vit_matrices import axis_permutation,MATRIX_OUTPUT_TO_RAW
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--seed',type=int,default=2017);args=parser.parse_args()
base=Path('release/native-vit');root=base/f'attention64-{args.seed}';weight=base/'block31-projection.weights'
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block31.layer4.layer',str(weight)],check=True,capture_output=True)
n=65536;t=bits(n,[2,6,7,8,14,15]);c=bits(n,[0,1,3,4,5,9,10,11,12,13])
residual=F(np.random.default_rng(args.seed).normal(0,1,(64,1024)).astype(np.float32));quantize(residual)[t,c].tofile(root/'projection-residual.fp8')
subprocess.run(['/tmp/native-vit-contract-oracle',str(root/'attention.fp8'),str(root/'projection-residual.fp8'),str(weight),str(root/'projection.fp8'),'64','8','projection'],check=True)
def decode(path):
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 result=np.empty((64,1024),np.float32);result[t,c]=e4m3fn(raw[:n]);return result
x=decode(root/'attention.fp8');target=decode(root/'projection.fp8');raw=np.fromfile(weight,np.uint8)
matrix=np.empty((1024,1024),np.float32);matrix[bits(1048576,[6,3,9,7,8,10,11,12,13,14]),bits(1048576,[0,1,2,4,5,15,16,17,18,19])]=e4m3fn(raw[:1048576])
skip=raw[1048576:].view('<f2').astype(np.float32)[axis_permutation(1024,MATRIX_OUTPUT_TO_RAW)]
parts=[multiply(x[:,i*256:(i+1)*256],matrix[:,i*256:(i+1)*256],H(residual*skip) if i==0 else np.zeros_like(residual)) for i in range(4)]
result=parts[0]
for part in parts[1:]:result=H(result+part)
result=F(result);err=np.abs(result-target)
print(json.dumps({'seed':args.seed,'values':n,'different':int(np.count_nonzero(err)),'max_error':float(err.max())}),flush=True)
assert np.array_equal(result,target),'native projection differs'
from native_vit_linear_reference import unpack_residual,residual_projection
assert np.array_equal(residual_projection(x,residual,*unpack_residual(weight,1024)),target),'reusable projection reference differs'
