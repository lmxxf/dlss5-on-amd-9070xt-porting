"""Numerical split-Swin reference, validated against each original stage."""
from pathlib import Path
import json,os,subprocess
import numpy as np
from native_c32_reference import H,F
from native_c64_reference import multiply
from native_c32_normalize import normalize
from native_c32_softmax_sum import denominator
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize

def bits(count,positions):
 i=np.arange(count);out=np.zeros(count,np.int32)
 for b,p in enumerate(positions):out|=((i>>p)&1)<<b
 return out
def matrix(raw):
 result=np.empty((512,512),np.float32)
 result[bits(262144,[3,6,7,8,9,10,11,12,13]),bits(262144,[1,0,4,5,2,14,15,16,17])]=e4m3fn(raw)
 return result
def ffwd(x,params):
 mixed=F(multiply(x,params['pre']));parts=[]
 for g in range(8):
  expanded=multiply(mixed[...,g*64:(g+1)*64],params['expand'][g]);gate=np.clip(expanded,-4,4)
  poly=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
  parts.append(F(multiply(F(H(expanded*poly)),params['contract'][g])))
 return np.concatenate(parts,axis=-1)
def attention(tiles,qkv,bias,scales):
 q,k,v=[multiply(tiles,m) for m in qkv];parts=[]
 for head in range(16):
  sl=slice(head*32,(head+1)*32)
  qh=F(H(normalize(q[...,sl])*H(scales[head])));kh=F(normalize(k[...,sl]));vh=F(v[...,sl])
  score=H(qh@kh.transpose(0,2,1)+bias[head]);a=np.clip(H(score*np.float32(.044921875)+np.float32(1.30078125)),1.03125,1.5693359375)
  b=a.astype(np.float16).view(np.uint16).astype(np.uint32)
  exp=(((b<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
  prob=F(H(exp*H(1/denominator(exp))))
  parts.append(F(H(H(prob[:,:,:32]@vh[:,:32])+prob[:,:,32:]@vh[:,32:])))
 return np.concatenate(parts,axis=-1)

if __name__=='__main__':
 root=Path('release/native-c512');folder=root/'full-check';folder.mkdir(parents=True,exist_ok=True)
 fw=np.load(root/'ffwd-check/matrices.npz');fp=np.load(root/'projection-check/matrices.npz')
 raw=np.fromfile(root/'block23-2.weights',np.uint8)
 i=np.arange(262144);v_offsets=(i//1024)*3072+2048+i%1024
 qkv=[matrix(raw[v_offsets+offset]) for offset in (-2048,-1024,0)]
 bl=dict(head=bits(65536,[12,13,14,15]),query=bits(65536,[5,6,10,7,1,11]),key=bits(65536,[0,3,8,4,2,9]))
 bias=np.empty((16,64,64),np.float32);bias[bl['head'],bl['query'],bl['key']]=raw[0xc0000:0xe0000].view('<f2')
 scales=raw[0xe0000:].view('<f4')
 final_raw=np.fromfile(root/'block23-3.weights',np.uint8);projection=matrix(final_raw[:262144])
 channels=np.arange(512);order=(channels//16)*16+(channels%8)*2+(channels%16//8)
 skip=np.empty(512,np.float32);skip[order]=final_raw[262144:].view('<f2')
 perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
 env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
 for seed,scale in [(661,.25),(673,1.)]:
  x=F(np.random.default_rng(seed).normal(0,scale,(8,16,512)).astype(np.float32))
  quantize(x[...,perm]).reshape(8,16,32,16).transpose(2,0,1,3).copy().tofile(folder/'input.fp8')
  output=folder/'output.fp8'
  subprocess.run(['/tmp/native-split-global-oracle',str(folder/'input.fp8'),str(output),*[str(root/f'block23-{i}.weights') for i in range(4)],'16','8','0','native-inpview'],env=env,check=True,capture_output=True)
  branch=ffwd(x,fw);feature=F(multiply(branch,fp['matrix'],H(x*fp['skip'])))
  tiles=feature.reshape(1,8,2,8,512).transpose(0,2,1,3,4).reshape(2,64,512)
  attended=attention(tiles,qkv,bias,scales).reshape(1,2,8,8,512).transpose(0,2,1,3,4).reshape(x.shape)
  result=F(multiply(attended,projection,H(feature*skip)))
  reports={};fixture={'input':x}
  for suffix,predicted in [('.branch',branch),('.ffn',feature),('.attn',attended),('',result)]:
   original=np.fromfile(str(output)+suffix,np.uint8);assert not np.any(original[65536:]) and not np.any((original[:65536]&127)==127)
   target=e4m3fn(original[:65536].reshape(8,8192)[:,inverse]).reshape(2,4,4,4,512).transpose(0,2,1,3,4).reshape(x.shape)
   fixture['oracle_'+str(len(reports))]=target
   error=np.abs(predicted-target);reports[suffix or 'final']={'exact_fraction':float(np.mean(predicted==target)),'mae':float(error.mean()),'max_error':float(error.max())}
  print(json.dumps({'seed':seed,'scale':scale,'stages':reports},indent=2),flush=True)
  assert all(v['exact_fraction']==1. for v in reports.values()), 'split stage arithmetic differs'
  np.savez(folder/f'fixture-{seed}.npz',**fixture)
 np.savez(folder/'attention-matrices.npz',Q=qkv[0],K=qkv[1],V=qkv[2],bias=bias,scales=scales,P=projection,skip=skip)
