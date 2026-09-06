"""Native attention reference for verified 64/128/256-token probe contracts."""
import numpy as np
from native_c32_reference import H,F
from native_c32_softmax_sum import denominator

def attention(q,k,v):
 if q.shape not in ((64,1024),(128,1024),(256,1024)) or k.shape!=q.shape or v.shape!=q.shape:
  raise ValueError('only 64/128/256-token attention contracts are verified')
 n=q.shape[0]
 q,k,v=[a.reshape(n,32,32).transpose(1,0,2) for a in (q,k,v)]
 score=H(q@k.transpose(0,2,1))
 coefficient=np.array([0x2dbb],np.uint16).view(np.float16).astype(np.float32)[0]
 affine=np.clip(H(score*coefficient+np.float32(1.708984375)),1.439453125,1.9775390625)
 b=affine.astype(np.float16).view(np.uint16).astype(np.uint32)
 exp=(((b<<4)+0x4000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
 key_order=np.zeros(64,np.int32)
 for bit,destination in enumerate([4,0,1,3,2,5]):key_order|=((np.arange(64)>>bit)&1)<<destination
 den=denominator(exp[...,:64][...,np.argsort(key_order)])
 for start in range(64,n,64):
  den=H(den+denominator(exp[...,start:start+64][...,np.argsort(key_order)]))
 numerator=H(F(exp[...,:32])@v[:,:32])
 for start in range(32,n,32):
  numerator=H(numerator+F(exp[...,start:start+32])@v[:,start:start+32])
 return F(H(numerator*H(1/den))).transpose(1,0,2).reshape(n,1024)
