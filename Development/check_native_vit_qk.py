"""Separate matrix/normalization candidates from Q/K output coordinates."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import H,F
from native_c32_normalize import normalize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-vit');raw=np.fromfile(root/'block31-qkv.weights',np.uint8)
source=np.fromfile(root/'contract.fp8',np.uint8);x=np.empty((16,1024),np.float32)
x[bits(16384,[2,6,7,8]),bits(16384,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(source[:16384])
scales=raw[:128].view('<f4')
oi=bits(1048576,[6,3,9,7,8,10,11,12,13,14]);ii=bits(1048576,[0,1,2,4,5,15,16,17,18,19])
for part in range(2):
 weight=raw[128:].reshape(-1,3,1024)[:,part,:].ravel();matrix=np.empty((1024,1024),np.float32);matrix[oi,ii]=e4m3fn(weight)
 target_raw=np.fromfile(root/f'qkv-vcheck-{part}.fp8',np.uint8);assert not np.any(target_raw[16384:]) and not np.any((target_raw[:16384]&127)==127)
 target=e4m3fn(target_raw[:16384]);
 for reduction in ['serial','two_partitions']:
  projected=multiply(x,matrix) if reduction=='serial' else H(multiply(x[:,:512],matrix[:,:512])+multiply(x[:,512:],matrix[:,512:]))
  normalized=normalize(projected.reshape(16,32,32))
  for scaled in [False,True]:
   got=F(H(normalized*H(scales)[None,:,None])) if scaled else F(normalized)
   print(json.dumps({'part':part,'reduction':reduction,'scaled':scaled,'sorted_different':int(np.count_nonzero(np.sort(got.ravel())!=np.sort(target))),'range':[float(got.min()),float(got.max())],'target_range':[float(target.min()),float(target.max())]}),flush=True)
  order=np.zeros(32,np.int32);channels=np.arange(32)
  for bit,destination in enumerate([1,0,4,2,3]):order|=((channels>>bit)&1)<<destination
  ordered=np.empty((16,32,32),np.float32);ordered[...,order]=projected.reshape(16,32,32)
  normalized=normalize(ordered)[...,order]
  got=F(H(H(normalized*np.float32(5.65625))*H(scales)[None,:,None])) if part==0 else F(normalized)
  decoded=np.empty((16,1024),np.float32);decoded[bits(16384,[2,6,7,8]),bits(16384,[0,1,3,4,5,9,10,11,12,13])]=target
  print(json.dumps({'part':part,'reduction':reduction,'candidate':'tensor_channel_order_and_Q_sqrt32_scale','sorted_different':int(np.count_nonzero(np.sort(got.ravel())!=np.sort(target))),'input_layout_different':int(np.count_nonzero(got.reshape(16,1024)!=decoded))}),flush=True)
  if part==1 and reduction=='two_partitions':
   offsets=np.arange(16384);tokens=bits(16384,[2,6,7,8]);columns=bits(16384,[0,1,3,4,5,9,10,11,12,13])
   for a in range(14):
    for b in range(a+1,14):
     perm=offsets^((((offsets>>a)^(offsets>>b))&1)*((1<<a)|(1<<b)))
     decoded[tokens,columns]=target[perm]
     if np.array_equal(got.reshape(16,1024),decoded):print(json.dumps({'K_exact_physical_bit_swap':[a,b]}),flush=True)
print(json.dumps({'QK_verified':False,'reason':'histogram candidates only'}))
raise SystemExit(1)
