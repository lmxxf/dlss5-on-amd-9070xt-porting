"""Native C64/C128 FFN-only arithmetic against original weights and CUBIN."""
import json,os,subprocess,argparse
from pathlib import Path
import numpy as np
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=64);args=parser.parse_args()
C=args.channels;heads=C//32;hidden=4*C;b2=hidden*C;b3=b2+C*128;end3=b3+C*C
width,height,index,fs,ats,scale_offset,qkv_begin,projection_begin=(32,16,5,0x7010,0xf0b0,0xe0a0,0x70a0,0xe0b0) if C==64 else (16,8,9,0x18010,0x30130,0x2c120,0x18120,0x2c130)
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if C==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
count=width*height*C
root=Path(f'release/native-c{C}');folder=root/'ffn-layout'
layout=np.load(folder/'layout.npz');view=np.load(root/'view/mapping.npz')['cell_output_to_hwc'];inverse=np.argsort(view)
original=np.fromfile(root/f'block{index}.weights',np.uint8)
def run(w,input_path,output,scan=False):
 w.tofile(folder/'validate.weights')
 env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
 if scan:env.update(DLSS5_NATIVE_SCAN_OFFSET=str(fs),DLSS5_NATIVE_SCAN_COUNT=str(2*C))
 ww,hh=(8,8) if scan else (width,height)
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'validate.weights'),str(input_path),str(output),str(folder/'aux.fp8'),f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_inpview_fp8',str(ww),str(hh),str(ww//8),str(hh//8),str(heads),'7','0'],env=env,check=True,capture_output=True)
w=np.zeros_like(original);w.view('<f2')[ats//2:ats//2+C]=1;w.view('<f4')[scale_offset//4:scale_offset//4+heads]=1
run(w,folder/'ones.fp8',folder/'skip-scan.fp8',True)
probe=np.fromfile(folder/'skip-scan.fp8',np.uint8).reshape(2*C,4,16*C)[1::2,:,inverse].reshape(C,64,C)
present=np.any(probe!=0,axis=1);assert np.all(present.sum(1)==1)
skip_channels=np.argmax(present,axis=1);assert np.unique(skip_channels).size==C
skip=np.empty(C,np.float32);skip[skip_channels]=original.view('<f2')[fs//2:fs//2+C]
matrices=[]
for begin,end,out_count,in_count,key_out,key_in in [(0,b2,hidden,C,'w1_hidden','w1_input'),(b2,b3,C,hidden,'w2_output','w2_hidden'),(b3,end3,C,C,'w3_output','w3_input')]:
 matrix=np.zeros((out_count,in_count),np.float32);matrix[layout[key_out],layout[key_in]]=e4m3fn(original[begin:end]);matrices.append(matrix)
source=Path('release/native-c32/block4-aux.fp8' if C==64 else 'release/native-c64/block8-aux.fp8')
x=e4m3fn(np.fromfile(source,np.uint8)[:count]).reshape(C//16,height,width,16).transpose(1,2,0,3).reshape(height,width,C)
channels=np.arange(C);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1);x=x[...,perm]
w=original.copy();w[qkv_begin:qkv_begin+3*C*C]=0;w[projection_begin:projection_begin+C*C]=0;w.view('<f2')[ats//2:ats//2+C]=1
run(w,source,folder/'ffn-only.fp8')
target=e4m3fn(np.fromfile(folder/'ffn-only.fp8',np.uint8)[:count].reshape(-1,16*C)[:,inverse]).reshape(height//4,width//4,4,4,C).transpose(0,2,1,3,4).reshape(x.shape)
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
assert np.array_equal(predicted,target), 'real preceding-layer input FFN regression'
from encode_tinlayout_global import quantize
for seed,scale in [(44,.25),(55,3.)]:
 x=F(np.random.default_rng(seed).normal(0,scale,(height,width,C)).astype(np.float32))
 encoded=quantize(x[...,perm]).reshape(height,width,C//16,16).transpose(2,0,1,3).copy()
 encoded.tofile(folder/'heldout-input.fp8')
 run(w,folder/'heldout-input.fp8',folder/'heldout-output.fp8')
 target=e4m3fn(np.fromfile(folder/'heldout-output.fp8',np.uint8)[:count].reshape(-1,16*C)[:,inverse]).reshape(height//4,width//4,4,4,C).transpose(0,2,1,3,4).reshape(x.shape)
 result=reference(x)
 print(json.dumps({'seed':seed,'scale':scale,'exact_fraction':float(np.mean(result==target)),'max_error':float(np.abs(result-target).max())}))
 assert np.array_equal(result,target), 'held-out FFN arithmetic regression'
np.savez(folder/'matrices.npz',W1=matrices[0],W2=matrices[1],W3=matrices[2],skip=skip)
