"""Block48 native projection/merge and C256 Swin reference.

CPU/original exact on 16x16 random output fixtures, shifts 0 and 3.
Logical HWC inputs here; original main uses global16 with low channel bits swapped.
"""
import numpy as np
from native_split_reference import bits
from native_c64_reference import unpack_bytes,multiply,block
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
def unpack(path):
    raw=np.fromfile(path,np.uint8)
    if raw.size!=820784:raise ValueError('block48 record size')
    ordinary=np.zeros(689232,np.uint8)
    ordinary[:0x58000]=raw[:0x58000]
    ordinary[0x58010:0x58210]=raw[0x78000:0x78200]
    ordinary[0x58220:]=raw[0x78400:]
    matrix=np.empty((256,512),np.float32)
    matrix[bits(131072,[3,6,7,8,9,10,11,12]),bits(131072,[1,0,4,5,2,13,14,15,16])]=e4m3fn(raw[0x58000:0x78000])
    c=np.arange(256);order=(c//16)*16+(c%8)*2+(c%16//8)
    scale=np.empty(256,np.float32);scale[order]=raw[0x78200:0x78400].view('<f2')
    return matrix,scale,unpack_bytes(ordinary)
def upsample(x,skip,params,shift=0):
    matrix,scale,body=params;h,w,c=skip.shape
    if c!=256 or x.shape!=(h//2,w//2,512) or h%8 or w%8 or shift not in range(4):raise ValueError('upsample48 geometry')
    low=multiply(x,matrix)
    merged=F(H(np.repeat(np.repeat(low,2,0),2,1)+skip*scale))
    px=4 if shift&1 else 0;py=4 if shift&2 else 0;ww=(w+px+7)//8*8;hh=(h+py+7)//8*8
    padded=np.pad(merged,((py,hh-h-py),(px,ww-w-px),(0,0)))
    tiles=padded.reshape(hh//8,8,ww//8,8,256).transpose(0,2,1,3,4).reshape(-1,64,256)
    result=block(tiles,*body).reshape(hh//8,ww//8,8,8,256).transpose(0,2,1,3,4).reshape(hh,ww,256)
    return result[py:py+h,px:px+w]
