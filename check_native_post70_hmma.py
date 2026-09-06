"""Test measured HMMA product alignment in the post70 two-K16 output head."""
from pathlib import Path
import json,argparse
import numpy as np
from native_c32_reference import unpack_bytes,block,H
from native_split_reference import bits
p=argparse.ArgumentParser();p.add_argument('--seed',type=int,default=2801);a=p.parse_args()
root=Path('release/native-post70');fixture=root/f'spatial-{a.seed}'
raw=np.fromfile(root/'smoke/weights.bin',np.uint8)
ordinary=np.zeros(20672,np.uint8);ordinary[:0x2050]=raw[:0x2050];ordinary[0x2060:]=raw[0x20d0:0x5130]
order=np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31])
sm=np.empty(32,np.float32);ss=np.empty(32,np.float32);sm[order]=raw[0x2050:0x2090].view('<f2');ss[order]=raw[0x2090:0x20d0].view('<f2')
head=np.empty((16,32),np.float32);head[bits(512,[2,5,6,7]),bits(512,[0,1,3,4,8])]=raw[0x5130:].view('<f2');head=head[[0,2,4]]
x=np.fromfile(fixture/'main-hwc.f32','<f4').reshape(8,8,32);skip=np.fromfile(fixture/'skip-hwc.f32','<f4').reshape(16,16,32)
merged=H(H(np.repeat(np.repeat(x,2,0),2,1)*sm)+skip*ss)
features=block(merged.reshape(2,8,2,8,32).transpose(0,2,1,3,4).reshape(4,64,32),unpack_bytes(ordinary),raw_output=True).reshape(2,2,8,8,32).transpose(0,2,1,3,4).reshape(256,32)
def aligned(a,b,acc):
    product=a[:,None,:].astype(np.float64)*b[None,:,:].astype(np.float64)
    exponents=np.frexp(abs(a))[1][:,None,:]+np.frexp(abs(b))[1][None,:,:]
    exponent=np.max(np.where(product!=0,exponents,-1000),axis=-1);exponent=np.where(exponent==-1000,0,exponent)
    quantum=np.exp2(exponent-27)
    return H(np.trunc(product/quantum[...,None]).sum(-1)*quantum+acc.astype(np.float64))
value=aligned(features[:,:16],head[:,:16],np.zeros((256,3),np.float32))
value=aligned(features[:,16:],head[:,16:],value)
# Preserve original float32 RGB encode/add/decode order; algebraic folding
# into base + residual changes the last bit at cancellation boundaries.
encoded=np.float32(value*np.float32(.03125)+np.float32(-.03125))
got=np.clip(np.float32(encoded*np.float32(8)+np.float32(.5)),0,1).reshape(16,16,3)
target=np.fromfile(fixture/'global-cell-out.f32','<f4').reshape(16,16,4)[:,:,:3]
err=np.abs(got-target);report={'different':int(np.count_nonzero(err)),'max_error':float(err.max()),'scope':'single spatial post70 HMMA candidate, not general accumulator proof'}
report['status']='pass' if report['different']==0 else 'fail'
(fixture/'hmma-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','post70 aligned head differs'
