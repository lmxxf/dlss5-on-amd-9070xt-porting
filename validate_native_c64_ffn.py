"""Native C64 FFN-only arithmetic against original weights and CUBIN."""
import json,os,subprocess
from pathlib import Path
import numpy as np
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c64');folder=root/'ffn-layout'
layout=np.load(folder/'layout.npz');view=np.load(root/'view/mapping.npz')['cell_output_to_hwc'];inverse=np.argsort(view)
original=np.fromfile(root/'block5.weights',np.uint8)
def run(w,input_path,output,scan=False):
 w.tofile(folder/'validate.weights')
 env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
 if scan:env.update(DLSS5_NATIVE_SCAN_OFFSET='0x7010',DLSS5_NATIVE_SCAN_COUNT='128')
 width,height,gx,gy=('8','8','1','1') if scan else ('32','16','4','2')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'validate.weights'),str(input_path),str(output),str(folder/'aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8',width,height,gx,gy,'2','7','0'],env=env,check=True,capture_output=True)
w=np.zeros_like(original);w.view('<f2')[0xf0b0//2:0xf130//2]=1;w.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
run(w,folder/'ones.fp8',folder/'skip-scan.fp8',True)
probe=np.fromfile(folder/'skip-scan.fp8',np.uint8).reshape(128,4,1024)[1::2,:,inverse].reshape(64,64,64)
present=np.any(probe!=0,axis=1);assert np.all(present.sum(1)==1)
skip_channels=np.argmax(present,axis=1);assert np.unique(skip_channels).size==64
skip=np.empty(64,np.float32);skip[skip_channels]=original.view('<f2')[0x7010//2:0x7090//2]
matrices=[]
for begin,end,out_count,in_count,key_out,key_in in [(0,0x4000,256,64,'w1_hidden','w1_input'),(0x4000,0x6000,64,256,'w2_output','w2_hidden'),(0x6000,0x7000,64,64,'w3_output','w3_input')]:
 matrix=np.zeros((out_count,in_count),np.float32);matrix[layout[key_out],layout[key_in]]=e4m3fn(original[begin:end]);matrices.append(matrix)
source=Path('release/native-c32/block4-aux.fp8')
x=e4m3fn(np.fromfile(source,np.uint8)[:32768]).reshape(4,16,32,16).transpose(1,2,0,3).reshape(16,32,64)
channels=np.arange(64);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1);x=x[...,perm]
w=original.copy();w[0x70a0:0xa0a0]=0;w[0xe0b0:0xf0b0]=0;w.view('<f2')[0xf0b0//2:0xf130//2]=1
run(w,source,folder/'ffn-only.fp8')
target=e4m3fn(np.fromfile(folder/'ffn-only.fp8',np.uint8)[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(x.shape)
def multiply(a,m,initial=None):
 result=np.zeros((*a.shape[:-1],m.shape[0]),np.float32) if initial is None else initial.copy()
 for start in range(0,m.shape[1],32):result=H(result+a[...,start:start+32]@m[:,start:start+32].T)
 return result
def reference(x):
 expanded=multiply(F(x),matrices[0]);gate=np.clip(expanded,-4,4)
 poly=H(gate*H(np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 hidden=F(H(expanded*poly));middle=F(multiply(hidden,matrices[1]))
 return F(multiply(middle,matrices[2],H(x*skip)))
predicted=reference(x)
error=np.abs(predicted-target)
print(json.dumps({'exact_fraction':float(np.mean(predicted==target)),'mae':float(error.mean()),'max_error':float(error.max()),'correlation':float(np.corrcoef(predicted.ravel(),target.ravel())[0,1]),'skip_channels':skip_channels.tolist()},indent=2))
assert np.array_equal(predicted,target), 'real block4 input FFN regression'
from encode_tinlayout_global import quantize
for seed,scale in [(44,.25),(55,3.)]:
 x=F(np.random.default_rng(seed).normal(0,scale,(16,32,64)).astype(np.float32))
 encoded=quantize(x[...,perm]).reshape(16,32,4,16).transpose(2,0,1,3).copy()
 encoded.tofile(folder/'heldout-input.fp8')
 run(w,folder/'heldout-input.fp8',folder/'heldout-output.fp8')
 target=e4m3fn(np.fromfile(folder/'heldout-output.fp8',np.uint8)[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(x.shape)
 result=reference(x)
 print(json.dumps({'seed':seed,'scale':scale,'exact_fraction':float(np.mean(result==target)),'max_error':float(np.abs(result-target).max())}))
 assert np.array_equal(result,target), 'held-out FFN arithmetic regression'
np.savez(folder/'matrices.npz',W1=matrices[0],W2=matrices[1],W3=matrices[2],skip=skip)
