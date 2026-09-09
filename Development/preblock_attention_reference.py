"""Attention-only original preblock reference. Arithmetic still under validation."""
import argparse,json
from pathlib import Path
import numpy as np
from preblock_mix_reference import inputs,unpack_mix
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
from native_c32_normalize import normalize as native_normalize
from native_c32_softmax_sum import denominator
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--prepare',action='store_true');p.add_argument('--native-exp',action='store_true');p.add_argument('--half-norm',action='store_true');p.add_argument('--export-lab',type=Path);p.add_argument('--sum-order',default='1,2,4,8,16,32');a=p.parse_args();a.folder.mkdir(parents=True,exist_ok=True)
raw=np.fromfile('/tmp/block0.weights',np.uint8);original=raw.view('<f2').copy()
if a.prepare:
 w=original.copy();w[:4096]=0;w[4616:4648]=1;w.tofile(a.folder/'attention-only.weights')
else:
 h=lambda x:np.asarray(x,np.float16).astype(np.float32)
 f8=lambda x:e4m3fn(quantize(x))
 layout=np.load('release/preblock-attention-layout/matrix-layout.npz')
 bias=np.load('release/preblock-attention-layout/bias-layout.npz')['bias']
 def matrix(begin,kind):
  out=np.empty((32,32),np.float32)
  out[layout[kind+'_output'],layout[kind+'_input']]=e4m3fn(raw[begin:begin+1024])
  return out
 qw,kw,vw=[matrix(9312+i*1024,'v') for i in range(3)]
 pw=matrix(20592,'projection');skip=np.empty(32,np.float32);skip[layout['skip_channel']]=original[10808:10840]
 scale=np.frombuffer(raw.tobytes(),'<f4',count=1,offset=20576)[0]
 rgb=np.fromfile('release/preblock-branch-audit/input.rgba32f','<f4').reshape(-1,8,8,4)
 prefix=h(np.stack([inputs(t[:,:,:3])@unpack_mix(original).T for t in rgb])).reshape(-1,64,32)
 feature=f8(prefix)
 q,k,v=[h(feature@w.T) for w in [qw,kw,vw]]
 def normalize(v):
  if not a.half_norm:return h(v/np.sqrt(np.maximum(np.sum(v*v,-1,keepdims=True),1e-12)))
  return native_normalize(v)
 q,k=normalize(q),normalize(k)
 if a.native_exp:
  scores=h(f8(h(q*h(scale)))@f8(k).transpose(0,2,1)+bias)
  affine=h(scores*np.float32(.044921875)+np.float32(1.30078125))
  bits=np.clip(affine,1.03125,1.5693359375).astype(np.float16).view(np.uint16).astype(np.uint32)
  exp=(((bits<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
 else:
  scores=h(h(f8(q)@f8(k).transpose(0,2,1))*h(scale)+bias)
  exp=np.exp(scores-scores.max(-1,keepdims=True))
 den=exp.sum(-1,keepdims=True)
 if a.half_norm:
  den=exp
  if a.sum_order=='sass':
   den=denominator(exp)
  elif a.sum_order=='native-lanes':
   partial=h(exp[...,:8]+exp[...,8:16])
   for offset in (16,32,48):partial=h(partial+h(exp[...,offset:offset+8]+exp[...,offset+8:offset+16]))
   lanes=h(partial[...,:2]+partial[...,2:4])
   for offset in (4,6):lanes=h(lanes+partial[...,offset:offset+2])
   den=h(lanes[...,:1]+lanes[...,1:2])
  else:
   order=[int(x) for x in a.sum_order.split(',')];assert sorted(order)==[1,2,4,8,16,32]
   indices=np.arange(64)
   for bit in order:
    keep=np.flatnonzero((indices&bit)==0)
    partner=np.array([np.flatnonzero(indices==(indices[i]^bit))[0] for i in keep])
    den=h(den[...,keep]+den[...,partner]);indices=indices[keep]
 pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
 target=e4m3fn(np.fromfile(a.folder/'attention-main.fp8',np.uint8).reshape(-1,2048)[:,pm]).reshape(-1,64,32)
 if a.export_lab:
  a.export_lab.mkdir(parents=True,exist_ok=True)
  np.concatenate([qw.ravel(),kw.ravel(),vw.ravel(),pw.ravel(),bias.ravel(),[scale],skip]).astype('<f4').tofile(a.export_lab/'weights.f32')
  prefix.astype('<f4').tofile(a.export_lab/'input.f32');target.astype('<f4').tofile(a.export_lab/'oracle.f32')
 reports=[]
 for mode in ['float','half','prob_fp8','exp_fp8','native_prob_fp8']:
  if mode=='float':av=h((exp/den)@f8(v))
  elif mode=='half':av=h(h(exp/den)@f8(v))
  elif mode=='prob_fp8':av=h(f8(exp/den)@f8(v))
  elif mode=='exp_fp8':av=h((f8(exp)@f8(v))/den)
  else:
   prob=f8(h(exp*h(1/den)))
   av=np.zeros_like(v)
   for start in [0,32]:av=h(av+prob[:,:,start:start+32]@f8(v[:,start:start+32]))
  pred=f8(h((f8(av)@pw.T)+h(prefix*skip)));err=np.abs(pred-target)
  reports.append({'mode':mode,'correlation':float(np.corrcoef(pred.ravel(),target.ravel())[0,1]),'exact_fraction':float(np.mean(pred==target)),'mae':float(err.mean()),'max_error':float(err.max())})
 print(json.dumps({'scale':float(scale),'arithmetic_candidates':reports},indent=2))
 if a.native_exp and a.half_norm and a.sum_order=='sass':
  assert np.array_equal(pred,target), 'Attention-only original-CUBIN regression'
