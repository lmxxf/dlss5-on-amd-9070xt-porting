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
    if raw.size==820784:C,n,begin,ffskip,qkv_old=256,689232,0x58000,0x78000,0x58220
    elif raw.size==230176:C,n,begin,ffskip,qkv_old=128,197184,0x18000,0x20000,0x18120
    else:raise ValueError('native upsample record size')
    ordinary=np.zeros(n,np.uint8)
    ordinary[:begin]=raw[:begin]
    ordinary[begin+16:begin+16+2*C]=raw[ffskip:ffskip+2*C]
    ordinary[qkv_old:]=raw[ffskip+4*C:]
    ob=C.bit_length()-1;count=2*C*C
    matrix=np.empty((C,2*C),np.float32)
    matrix[bits(count,[3]+list(range(6,ob+5))),bits(count,[1,0,4,5,2]+list(range(ob+5,2*ob+1)))]=e4m3fn(raw[begin:ffskip])
    c=np.arange(C);order=(c//16)*16+(c%8)*2+(c%16//8)
    scale=np.empty(C,np.float32);scale[order]=raw[ffskip+2*C:ffskip+4*C].view('<f2')
    return matrix,scale,unpack_bytes(ordinary)
def upsample(x,skip,params,shift=0):
    matrix,scale,body=params;h,w,c=skip.shape
    if c not in (128,256) or x.shape!=(h//2,w//2,2*c) or h%8 or w%8 or shift not in range(4):raise ValueError('native upsample geometry')
    low=multiply(x,matrix)
    merged=F(H(np.repeat(np.repeat(low,2,0),2,1)+skip*scale))
    px=4 if shift&1 else 0;py=4 if shift&2 else 0;ww=(w+px+7)//8*8;hh=(h+py+7)//8*8
    padded=np.pad(merged,((py,hh-h-py),(px,ww-w-px),(0,0)))
    tiles=padded.reshape(hh//8,8,ww//8,8,c).transpose(0,2,1,3,4).reshape(-1,64,c)
    result=block(tiles,*body).reshape(hh//8,ww//8,8,8,c).transpose(0,2,1,3,4).reshape(hh,ww,c)
    return result[py:py+h,px:px+w]
