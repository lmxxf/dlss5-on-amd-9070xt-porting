"""Native C32 upsample candidate; preserve half merge before the C32 block."""
import numpy as np
from native_c32_reference import unpack_bytes,block,H
from native_c64_reference import multiply
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
def unpack(path):
    raw=np.fromfile(path,np.uint8)
    if raw.size!=22784:raise ValueError('block66 record')
    ordinary=np.zeros(20672,np.uint8);ordinary[:0x2000]=raw[:0x2000];ordinary[0x2000:0x2060]=raw[0x2800:0x2860];ordinary[0x2060:]=raw[0x28a0:]
    matrix=np.empty((32,64),np.float32);matrix[bits(2048,[3,6,7,8,9]),bits(2048,[1,0,4,5,2,10])]=e4m3fn(raw[0x2000:0x2800])
    order=np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31])
    c=np.arange(32);other=(c//16)*16+(c%8)*2+(c%16//8)
    reordered=np.empty_like(matrix);reordered[order]=matrix[other]
    scale=np.empty(32,np.float32);scale[order]=raw[0x2860:0x28a0].view('<f2')
    return reordered,scale,unpack_bytes(ordinary)
def upsample(x,skip,params,shift=0):
    matrix,scale,body=params;h,w,c=skip.shape
    if c!=32 or x.shape!=(h//2,w//2,64) or h%8 or w%8 or shift not in range(4):raise ValueError('C32 upsample shape')
    low=multiply(x,matrix);merged=H(np.repeat(np.repeat(low,2,0),2,1)+skip*scale)
    px=4 if shift&1 else 0;py=4 if shift&2 else 0;ww=(w+px+7)//8*8;hh=(h+py+7)//8*8
    padded=np.pad(merged,((py,hh-h-py),(px,ww-w-px),(0,0)))
    tiles=padded.reshape(hh//8,8,ww//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
    result=block(tiles,body).reshape(hh//8,ww//8,8,8,32).transpose(0,2,1,3,4).reshape(hh,ww,32)
    return result[py:py+h,px:px+w]
