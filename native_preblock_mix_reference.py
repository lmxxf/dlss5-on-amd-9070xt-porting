"""Measured half-input HMMA mixing: exponent alignment, truncation, integer sum."""
import numpy as np
from native_c32_reference import H

def tensor_mix(features,weight):
 a=np.asarray(features,np.float32);b=np.asarray(weight,np.float32)
 if a.shape[-1]!=16 or b.shape!=(32,16):raise ValueError('input-mix shape')
 assert np.isfinite(a).all() and np.isfinite(b).all() and np.array_equal(H(a),a) and np.array_equal(H(b),b)
 flat=a.reshape(-1,16);result=np.empty((len(flat),32),np.float32);be=np.frexp(np.abs(b))[1]
 for start in range(0,len(flat),4096):
  x=flat[start:start+4096];product=x[:,None,:].astype(np.float64)*b[None,:,:].astype(np.float64)
  exponent=np.max(np.where(product!=0,np.frexp(np.abs(x))[1][:,None,:]+be[None,:,:],-1000),axis=-1)
  exponent=np.where(exponent==-1000,0,exponent)
  quantum=np.exp2(exponent-27)
  result[start:start+4096]=H(np.trunc(product/quantum[...,None]).sum(-1)*quantum)
 return result.reshape(*a.shape[:-1],32)
