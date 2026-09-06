"""Validate split FFN output projection using original matrix and skip values."""
from pathlib import Path
import json,os,subprocess
import numpy as np
from native_c32_reference import H,F
from native_c64_reference import multiply
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512');folder=root/'projection-check';folder.mkdir(parents=True,exist_ok=True)
params=np.load(root/'ffwd-check/matrices.npz');raw=np.fromfile(root/'block23-1.weights',np.uint8)
def bits(positions):
 i=np.arange(262144);v=np.zeros(262144,np.int32)
 for b,p in enumerate(positions):v|=((i>>p)&1)<<b
 return v
matrix=np.empty((512,512),np.float32);matrix[bits([3,6,7,8,9,10,11,12,13]),bits([1,0,4,5,2,14,15,16,17])]=e4m3fn(raw[:262144])
s=np.arange(512);order=(s//16)*16+(s%8)*2+(s%16//8);skip=np.empty(512,np.float32);skip[order]=raw[262144:].view('<f2')
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
perm=(s&~3)|((s&1)<<1)|((s&2)>>1)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
for seed,scale in [(631,.25),(641,1.)]:
 x=F(np.random.default_rng(seed).normal(0,scale,(8,16,512)).astype(np.float32))
 quantize(x[...,perm]).reshape(8,16,32,16).transpose(2,0,1,3).copy().tofile(folder/'input.fp8')
 output=folder/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(folder/'input.fp8'),str(output),str(root/'block23-0.weights'),str(root/'block23-1.weights'),str(root/'gate-probe/w2.bin'),str(root/'gate-probe/w3.bin'),'16','8','0','native-inpview'],env=env,check=True,capture_output=True)
 target_raw=np.fromfile(str(output)+'.ffn',np.uint8);assert not np.any(target_raw[65536:]) and not np.any((target_raw[:65536]&127)==127)
 target=e4m3fn(target_raw[:65536].reshape(8,8192)[:,inverse]).reshape(2,4,4,4,512).transpose(0,2,1,3,4).reshape(x.shape)
 mixed=F(multiply(x,params['pre']));parts=[]
 for g in range(8):
  expanded=multiply(mixed[...,g*64:(g+1)*64],params['expand'][g]);gate=np.clip(expanded,-4,4)
  poly=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
  parts.append(F(multiply(F(H(expanded*poly)),params['contract'][g])))
 predicted=F(multiply(np.concatenate(parts,axis=-1),matrix,H(x*skip)))
 error=np.abs(predicted-target);print(json.dumps({'seed':seed,'scale':scale,'exact_fraction':float(np.mean(predicted==target)),'mae':float(error.mean()),'max_error':float(error.max())}),flush=True)
 assert np.array_equal(predicted,target), 'split FFN output projection differs'
np.savez(folder/'matrices.npz',matrix=matrix,skip=skip)
