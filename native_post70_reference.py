"""Post70 texture-mask1/rgb-mode1 reference; optional blend branches excluded."""
import numpy as np
from native_c32_reference import unpack_bytes,block,H
from native_split_reference import bits
def unpack(path):
    raw=np.fromfile(path,np.uint8)
    if raw.size!=21808:raise ValueError('post70 weights')
    ordinary=np.zeros(20672,np.uint8);ordinary[:0x2050]=raw[:0x2050];ordinary[0x2060:]=raw[0x20d0:0x5130]
    order=np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31])
    sm=np.empty(32,np.float32);ss=np.empty(32,np.float32);sm[order]=raw[0x2050:0x2090].view('<f2');ss[order]=raw[0x2090:0x20d0].view('<f2')
    head=np.empty((16,32),np.float32);head[bits(512,[2,5,6,7]),bits(512,[0,1,3,4,8])]=raw[0x5130:].view('<f2')
    return unpack_bytes(ordinary),sm,ss,head[[0,2,4]]
def aligned(a,b,acc,acc_exponent_offset=1,truncate_acc=False):
    result=np.empty_like(acc)
    for start in range(0,len(a),4096):
        x=a[start:start+4096];product=x[:,None,:].astype(np.float64)*b[None,:,:].astype(np.float64)
        e=np.frexp(abs(x))[1][:,None,:]+np.frexp(abs(b))[1][None,:,:]
        e=np.max(np.where(product!=0,e,-1000),axis=-1);initial=acc[start:start+4096].astype(np.float64)
        if acc_exponent_offset is not None:e=np.maximum(e,np.where(initial!=0,np.frexp(abs(initial))[1]+acc_exponent_offset,-1000))
        e=np.where(e==-1000,0,e);quantum=np.exp2(e-27)
        if truncate_acc:initial=np.trunc(initial/quantum)*quantum
        result[start:start+4096]=H(np.trunc(product/quantum[...,None]).sum(-1)*quantum+initial)
    return result
def post(main,skip,color,params,input_scale=.03125):
    h,w,c=skip.shape
    if c!=32 or h%16 or w%16 or main.shape!=(h//2,w//2,32) or color.shape!=(h,w,3):raise ValueError('post70 shape')
    body,sm,ss,head=params
    merged=H(H(np.repeat(np.repeat(main,2,0),2,1)*sm)+skip*ss)
    tiles=merged.reshape(h//8,8,w//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
    features=block(tiles,body,raw_output=True).reshape(h//8,w//8,8,8,32).transpose(0,2,1,3,4).reshape(h*w,32)
    value=aligned(features[:,:16],head[:,:16],np.zeros((h*w,3),np.float32))
    value=aligned(features[:,16:],head[:,16:],value)
    base=np.float32(color.reshape(-1,3).astype(np.float64)*.125-.0625)
    encoded=np.float32(value.astype(np.float64)*np.float32(input_scale)+base.astype(np.float64))
    return np.clip(np.float32(encoded.astype(np.float64)*8+.5),0,1).reshape(h,w,3)
