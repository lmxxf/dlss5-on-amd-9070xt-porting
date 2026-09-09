"""Compare native ViT contraction with explicit, global reduction candidates."""
from pathlib import Path
import subprocess,json,itertools,argparse
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
from unpack_vit_matrices import axis_permutation,MATRIX_OUTPUT_TO_RAW
parser=argparse.ArgumentParser();parser.add_argument('--folder',type=Path,default=Path('release/native-vit'));parser.add_argument('--replays',type=int,default=1);args=parser.parse_args()
if not 1<=args.replays<=5:parser.error('replays must be 1..5')
root=args.folder;weight_path=Path('release/native-vit/block31-contract.weights');baseline=None
for replay in range(args.replays):
 subprocess.run(['/tmp/native-vit-contract-oracle',str(root/'expand.fp8'),str(root/'input.fp8'),str(weight_path),str(root/'contract.fp8'),'16','8'],check=True)
 data=(root/'contract.fp8').read_bytes()
 if baseline is None:baseline=data
 else:assert data==baseline,'original contraction changed between identical runs'
def decode(path,C):
 n=16*C;raw=np.fromfile(path,np.uint8);assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 a=np.empty((16,C),np.float32);a[bits(n,[2,6,7,8]),bits(n,[0,1,3,4,5]+list(range(9,9+C.bit_length()-6)))]=e4m3fn(raw[:n]);return a
x=decode(root/'expand.fp8',4096);residual=decode(root/'input.fp8',1024);target=decode(root/'contract.fp8',1024)
raw=np.fromfile(weight_path,np.uint8);matrix=np.empty((1024,4096),np.float32)
matrix[bits(4194304,[6,3,9,7,8,10,11,12,13,14]),bits(4194304,[0,1,2,4,5,15,16,17,18,19,20,21])]=e4m3fn(raw[:4194304])
skip=raw[4194304:].view('<f2').astype(np.float32)[axis_permutation(1024,MATRIX_OUTPUT_TO_RAW)]
bias=H(residual*skip)
full=F(multiply(x,matrix,bias))
print(json.dumps({'candidate':'serial_K32','different':int(np.count_nonzero(full!=target)),'max_error':float(np.max(np.abs(full-target)))}),flush=True)
best=target.size+1
native_exact=False
for kind in ['first_partition_residual','quarter_residual','residual_after_sum']:
 parts=[]
 for group in range(4):
  initial=bias if kind=='first_partition_residual' and group==0 else H(bias*.25) if kind=='quarter_residual' else np.zeros_like(bias)
  parts.append(multiply(x[:,group*1024:(group+1)*1024],matrix[:,group*1024:(group+1)*1024],initial))
 for order in itertools.permutations(range(4)):
  value=parts[order[0]]
  for i in order[1:]:value=H(value+parts[i])
  if kind=='residual_after_sum':value=H(value+bias)
  predicted=F(value);different=int(np.count_nonzero(predicted!=target))
  if kind=='first_partition_residual' and order==(0,1,2,3):native_exact=different==0
  if different<best:
   best=different;print(json.dumps({'candidate':kind,'order':order,'different':different,'max_error':float(np.max(np.abs(predicted-target)))}),flush=True)
assert native_exact,'native first-partition residual and Z0/1/2/3 reduction differ'
from native_vit_linear_reference import unpack_residual,residual_projection
assert np.array_equal(residual_projection(x,residual,*unpack_residual(weight_path,4096)),target),'reusable contraction reference differs'
print(json.dumps({'native_contract_exact':True,'replays':args.replays,'fixture':str(root)}))
