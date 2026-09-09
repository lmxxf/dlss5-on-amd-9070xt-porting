"""Test native ViT expansion using original record and 1d layout candidates."""
from pathlib import Path
import subprocess,json,argparse
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import F,H
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
parser=argparse.ArgumentParser();parser.add_argument('--random-seed',type=int);args=parser.parse_args()
root=Path('release/native-rgb256');folder=Path('release/native-vit')
if args.random_seed is not None:folder=folder/f'random-{args.random_seed}'
folder.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block31.layer0.layer',str(folder/'block31-expand.weights')],check=True,capture_output=True)
head=np.fromfile(root/'block30-head.fp8',np.uint8);repack=np.fromfile(root/'repack-output-to-input.i32','<u4');assert repack.size==16384 and repack.max()<16384
input=head[repack]
if args.random_seed is not None:input=quantize(np.random.default_rng(args.random_seed).normal(0,1,16384).astype(np.float32))
input.tofile(folder/'input.fp8')
subprocess.run(['/tmp/native-vit-expand-oracle',str(folder/'input.fp8'),str(folder/'expand.fp8'),str(folder/'block31-expand.weights'),'16','32'],check=True)
raw=np.fromfile(folder/'expand.fp8',np.uint8);n=16*4096
assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
x=np.empty((16,1024),np.float32);x[bits(16384,[2,6,7,8]),bits(16384,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(input)
weights=np.fromfile(folder/'block31-expand.weights',np.uint8);matrix=np.empty((4096,1024),np.float32)
matrix[bits(4194304,[3,6,7,8,9,10,11,12,13,14,15,16]),bits(4194304,[1,0,4,5,2,17,18,19,20,21])]=e4m3fn(weights[:4194304])
predicted=F(multiply(x,matrix));target=np.empty_like(predicted)
target[bits(n,[2,6,7,8]),bits(n,[0,1,3,4,5,9,10,11,12,13,14,15])]=e4m3fn(raw[:n])
print(json.dumps({'values':n,'different':int(np.count_nonzero(predicted!=target)),'sorted_different':int(np.count_nonzero(np.sort(predicted.ravel())!=np.sort(target.ravel()))),'max_error':float(np.max(np.abs(predicted-target)))}),flush=True)
from unpack_vit_matrices import unpack_matrix
legacy=unpack_matrix(weights[:4194304].tobytes(),1024,4096,'matrix_input','matrix_output').astype(np.float32).T
legacy_predicted=F(multiply(x,legacy))
print(json.dumps({'candidate':'legacy_direct_unpack','different':int(np.count_nonzero(legacy_predicted!=target)),'sorted_different':int(np.count_nonzero(np.sort(legacy_predicted.ravel())!=np.sort(target.ravel()))),'max_error':float(np.max(np.abs(legacy_predicted-target)))}),flush=True)
cell_inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
canonical=e4m3fn(head[:16384]).reshape(2,8192)[:,cell_inverse].reshape(2,4,4,512).transpose(1,2,0,3).reshape(16,1024)
for name,m in ([] if args.random_seed is not None else [('swin_bits_canonical_input',matrix),('legacy_canonical_input',legacy)]):
 got=F(multiply(canonical,m))
 print(json.dumps({'candidate':name,'different':int(np.count_nonzero(got!=target)),'sorted_different':int(np.count_nonzero(np.sort(got.ravel())!=np.sort(target.ravel())))}),flush=True)
ids=np.arange(16384,dtype=np.int32).reshape(4,4,2,512).transpose(2,0,1,3).reshape(2,8192)
physical=np.empty_like(ids);physical[:,cell_inverse]=ids
mapping=physical.ravel()[repack]
print(json.dumps({'1d_physical_bit_to_canonical_bitmask':[int(mapping[1<<b]^mapping[0]) for b in range(14)]}),flush=True)
measured=np.empty((4096,1024),np.float32)
measured[bits(4194304,[6,3,9,7,8,10,11,12,13,14,15,16]),bits(4194304,[0,1,2,4,5,17,18,19,20,21])]=e4m3fn(weights[:4194304])
expanded=multiply(x,measured);gate=np.clip(expanded,-4,4)
poly=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
correct=F(H(expanded*poly))
error=np.abs(correct-target)
print(json.dumps({'candidate':'measured_bits_and_fused_activation','different':int(np.count_nonzero(error)),'max_error':float(error.max())}),flush=True)
assert np.array_equal(correct,target),'measured native ViT expansion differs'
from native_vit_linear_reference import unpack_expand,expand
assert np.array_equal(expand(x,unpack_expand(folder/'block31-expand.weights')),target),'reusable expansion reference differs'
measured.tofile(folder/'expand-matrix.f32')
