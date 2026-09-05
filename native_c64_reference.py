"""Original-byte C64 block reference; no proxy weights or fitted corrections."""
from pathlib import Path
import json,os,subprocess
import numpy as np
from native_c32_reference import H,F
from native_c32_normalize import normalize
from native_c32_softmax_sum import denominator
from decode_tinlayout_global import e4m3fn

def multiply(a,m,initial=None):
 result=np.zeros((*a.shape[:-1],m.shape[0]),np.float32) if initial is None else initial.copy()
 for offset in range(0,m.shape[1],32):result=H(result+a[...,offset:offset+32]@m[:,offset:offset+32].T)
 return result

def block(x,ffn,qkv,projection,bias,scales,skip):
 expanded=multiply(F(x),ffn['W1']);gate=np.clip(expanded,-4,4)
 poly=H(gate*H(np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 hidden=F(H(expanded*poly));middle=F(multiply(hidden,ffn['W2']))
 feature=multiply(middle,ffn['W3'],H(x*ffn['skip']))
 q,k,v=[multiply(F(feature),m) for m in qkv]
 combined=[]
 for head in range(2):
  sl=slice(head*32,(head+1)*32)
  qh=F(H(normalize(q[...,sl])*H(scales[head])));kh=F(normalize(k[...,sl]));vh=F(v[...,sl])
  scores=H(qh@kh.transpose(0,2,1)+bias[head])
  affine=np.clip(H(scores*np.float32(.044921875)+np.float32(1.30078125)),1.03125,1.5693359375)
  bits=affine.astype(np.float16).view(np.uint16).astype(np.uint32)
  exp=(((bits<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
  prob=F(H(exp*H(1/denominator(exp))))
  av=H(H(prob[:,:,:32]@vh[:,:32])+prob[:,:,32:]@vh[:,32:])
  combined.append(F(av))
 return F(multiply(np.concatenate(combined,axis=-1),projection,H(feature*skip)))

if __name__=='__main__':
 root=Path('release/native-c64');folder=root/'attention-layout'
 original=np.fromfile(root/'block5.weights',np.uint8)
 layout=np.load(folder/'matrix-layout.npz');bl=np.load(folder/'bias-layout.npz')
 inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
 # Recover skip slot-to-output mapping independently with half high-byte probes.
 weights=np.zeros_like(original);weights.view('<f2')[0x7010//2:0x7090//2]=1
 weights.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1;weights.tofile(folder/'skip-control.weights')
 np.full(4096,0x38,np.uint8).tofile(folder/'skip-input.fp8')
 env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
 env.update(DLSS5_NATIVE_SCAN_OFFSET='0xf0b0',DLSS5_NATIVE_SCAN_COUNT='128')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'skip-control.weights'),str(folder/'skip-input.fp8'),str(folder/'skip-output.fp8'),str(folder/'unused.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','8','8','1','1','2','7','0'],env=env,check=True,capture_output=True)
 probe=np.fromfile(folder/'skip-output.fp8',np.uint8).reshape(128,4,1024)[1::2,:,inverse].reshape(64,64,64)
 present=np.any(probe!=0,axis=1);assert np.all(present.sum(1)==1)
 skip_channels=np.argmax(present,axis=1);assert np.unique(skip_channels).size==64
 skip=np.empty(64,np.float32);skip[skip_channels]=original.view('<f2')[0xf0b0//2:0xf130//2]
 qkv=[]
 for offset in (-0x800,-0x400,0):
  matrix=np.empty((64,64),np.float32);matrix[layout['v_output'],layout['v_input']]=e4m3fn(original[layout['v_offsets']+offset]);qkv.append(matrix)
 projection=np.empty((64,64),np.float32);projection[layout['p_output'],layout['p_input']]=e4m3fn(original[0xe0b0:0xf0b0])
 bias=np.empty((2,64,64),np.float32);bias[bl['head'],bl['query'],bl['key']]=original.view('<f2')[0xa0a0//2:0xe0a0//2]
 scales=original[0xe0a0:0xe0a8].view('<f4')
 raw=np.fromfile('release/native-c32/block4-aux.fp8',np.uint8)[:32768]
 x=e4m3fn(raw).reshape(4,16,32,16).transpose(1,2,0,3).reshape(16,32,64)
 c=np.arange(64);perm=(c&~3)|((c&1)<<1)|((c&2)>>1);x=x[...,perm]
 tiles=x.reshape(2,8,4,8,64).transpose(0,2,1,3,4).reshape(8,64,64)
 ffn=np.load(root/'ffn-layout/matrices.npz')
 y=block(tiles,ffn,qkv,projection,bias,scales,skip).reshape(2,4,8,8,64).transpose(0,2,1,3,4).reshape(x.shape)
 target=e4m3fn(np.fromfile(root/'block5-output.fp8',np.uint8)[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(x.shape)
 err=np.abs(y-target)
 print(json.dumps({'exact_fraction':float(np.mean(y==target)),'mae':float(err.mean()),'max_error':float(err.max()),'correlation':float(np.corrcoef(y.ravel(),target.ravel())[0,1])},indent=2))
 np.savez(folder/'full-matrices.npz',Q=qkv[0],K=qkv[1],V=qkv[2],P=projection,bias=bias,scales=scales,skip=skip)
 assert np.isfinite(y).all() and np.isfinite(target).all()
 assert np.array_equal(y,target), 'complete C64 arithmetic is not yet exact'
