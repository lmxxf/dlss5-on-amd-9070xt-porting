"""Test native ViT expansion using original record and 1d layout candidates."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb256');folder=Path('release/native-vit');folder.mkdir(exist_ok=True)
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block31.layer0.layer',str(folder/'block31-expand.weights')],check=True,capture_output=True)
head=np.fromfile(root/'block30-head.fp8',np.uint8);repack=np.fromfile(root/'repack-output-to-input.i32','<u4');assert repack.size==16384 and repack.max()<16384
input=head[repack];input.tofile(folder/'input.fp8')
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
assert np.array_equal(predicted,target) or np.array_equal(legacy_predicted,target),'ViT expansion candidates differ'
