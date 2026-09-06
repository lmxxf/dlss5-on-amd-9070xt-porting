"""Verified 64-token native attention; smaller-token call contract is unresolved."""
import numpy as np
from native_c32_reference import H,F
from native_c32_softmax_sum import denominator

def attention(q,k,v):
 if q.shape!=(64,1024) or k.shape!=q.shape or v.shape!=q.shape:
  raise ValueError('only the 64-token attention contract is verified')
 q,k,v=[a.reshape(64,32,32).transpose(1,0,2) for a in (q,k,v)]
 score=H(q@k.transpose(0,2,1))
 coefficient=np.array([0x2dbb],np.uint16).view(np.float16).astype(np.float32)[0]
 affine=np.clip(H(score*coefficient+np.float32(1.708984375)),1.439453125,1.9775390625)
 b=affine.astype(np.float16).view(np.uint16).astype(np.uint32)
 exp=(((b<<4)+0x4000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
 key_order=np.zeros(64,np.int32)
 for bit,destination in enumerate([4,0,1,3,2,5]):key_order|=((np.arange(64)>>bit)&1)<<destination
 den=denominator(exp[...,np.argsort(key_order)])
 numerator=H(H(F(exp[...,:32])@v[:,:32])+F(exp[...,32:])@v[:,32:])
 return F(H(numerator*H(1/den))).transpose(1,0,2).reshape(64,1024)
