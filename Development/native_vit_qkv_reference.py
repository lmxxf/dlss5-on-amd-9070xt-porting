"""Native ViT QKV math, independent of its physical buffer layouts."""
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from native_c32_normalize import normalize
from decode_tinlayout_global import e4m3fn

def unpack(path):
 raw=np.fromfile(path,np.uint8);assert raw.size==3145856
 scales=raw[:128].view('<f4').copy();assert np.isfinite(scales).all();matrices=[]
 oi=bits(1048576,[6,3,9,7,8,10,11,12,13,14]);ii=bits(1048576,[0,1,2,4,5,15,16,17,18,19])
 for part in range(3):
  m=np.empty((1024,1024),np.float32);m[oi,ii]=e4m3fn(raw[128:].reshape(-1,3,1024)[:,part,:].ravel());matrices.append(m)
 return matrices,scales

def qkv(x,matrices,scales):
 assert x.ndim==2 and x.shape[1]==1024
 projected=[H(multiply(x[:,:512],m[:,:512])+multiply(x[:,512:],m[:,512:])) for m in matrices]
 order=np.zeros(32,np.int32);channels=np.arange(32)
 for bit,destination in enumerate([1,0,4,2,3]):order|=((channels>>bit)&1)<<destination
 def norm(v):
  v=v.reshape(-1,32,32);ordered=np.empty_like(v);ordered[...,order]=v
  return normalize(ordered)[...,order]
 q=F(H(H(norm(projected[0])*np.float32(5.65625))*H(scales)[None,:,None])).reshape(x.shape)
 k=F(norm(projected[1])).reshape(x.shape)
 return q,k,F(projected[2])
