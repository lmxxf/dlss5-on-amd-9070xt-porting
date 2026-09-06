"""Check a three-matrix split ffwd layout against original coefficients."""
from pathlib import Path
import json,subprocess,os
import numpy as np
from native_c32_reference import H,F
from native_c64_reference import multiply
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512');folder=root/'ffwd-check';folder.mkdir(parents=True,exist_ok=True)
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
def bits(count,positions):
 i=np.arange(count);out=np.zeros(count,np.int32)
 for b,p in enumerate(positions):out|=((i>>p)&1)<<b
 return out
raw=np.fromfile(root/'block23-0.weights',np.uint8);assert raw.size==524288
pre=np.empty((512,512),np.float32)
pre[bits(262144,[3,6,7,8,9,10,11,12,13]),bits(262144,[1,0,4,5,2,14,15,16,17])]=e4m3fn(raw[:262144])
expand=np.empty((8,256,64),np.float32);contract=np.empty((8,64,256),np.float32)
for g in range(8):
 expand[g,bits(16384,[3,6,7,8,9,10,11,12]),bits(16384,[1,0,4,5,2,13])]=e4m3fn(raw[0x40000+g*16384:0x40000+(g+1)*16384])
 contract[g,bits(16384,[3,6,7,8,9,10]),bits(16384,[1,0,4,5,2,11,12,13])]=e4m3fn(raw[0x60000+g*16384:0x60000+(g+1)*16384])
for i,size in enumerate((263168,917568,263168),1):
 w=np.zeros(size,np.uint8)
 if i in (1,3):w.view('<f2')[262144//2:]=1
 if i==2:w.view('<f4')[917504//4:]=1
 w.tofile(folder/f'w{i}.bin')
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
channels=np.arange(512);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
for seed,scale in [(601,.25),(607,1.)]:
 x=F(np.random.default_rng(seed).normal(0,scale,(8,16,512)).astype(np.float32))
 quantize(x[...,perm]).reshape(8,16,32,16).transpose(2,0,1,3).copy().tofile(folder/'input.fp8')
 output=folder/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(folder/'input.fp8'),str(output),str(root/'block23-0.weights'),*[str(folder/f'w{i}.bin') for i in range(1,4)],'16','8','0','native-inpview'],check=True,capture_output=True,env=environment)
 original=np.fromfile(str(output)+'.branch',np.uint8);assert not np.any(original[65536:]) and not np.any((original[:65536]&127)==127)
 target=e4m3fn(original[:65536].reshape(8,8192)[:,inverse]).reshape(2,4,4,4,512).transpose(0,2,1,3,4).reshape(x.shape)
 mixed=F(multiply(x,pre));groups=[]
 for g in range(8):
  expanded=multiply(mixed[...,g*64:(g+1)*64],expand[g]);gate=np.clip(expanded,-4,4)
  poly=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
  hidden=F(H(expanded*poly));groups.append(F(multiply(hidden,contract[g])))
 predicted=np.concatenate(groups,axis=-1);err=np.abs(predicted-target)
 print(json.dumps({'seed':seed,'scale':scale,'exact_fraction':float(np.mean(predicted==target)),'mae':float(err.mean()),'max_error':float(err.max()),'sorted_values_equal':bool(np.array_equal(np.sort(predicted.ravel()),np.sort(target.ravel())))}),flush=True)
 assert np.array_equal(predicted,target), 'split ffwd layout/arithmetic not closed'
np.savez(folder/'matrices.npz',pre=pre,expand=expand,contract=contract)
