"""Block39 reference: native FP8 matrix, four half partitions, nearest upsample.

Validated against original spatial 8x8 fixtures; other extents need validation.
"""
import numpy as np
from native_split_reference import bits
from native_c32_reference import H,F
from native_c64_reference import multiply
from decode_tinlayout_global import e4m3fn

def unpack(path):
    raw=np.fromfile(path,np.uint8)
    if raw.size!=525312:raise ValueError('block39 record size')
    matrix=np.empty((512,1024),np.float32)
    matrix[bits(524288,[3,6,7,8,9,10,11,12,13]),
           bits(524288,[1,0,4,5,2,14,15,16,17,18])]=e4m3fn(raw[:524288])
    c=np.arange(512);order=(c//16)*16+(c%8)*2+(c%16//8)
    scale=np.empty(512,np.float32);scale[order]=raw[524288:].view('<f2')
    return matrix,scale

def project(x,matrix):
    if x.ndim!=3 or x.shape[-1]!=1024 or matrix.shape!=(512,1024):
        raise ValueError('block39 projection shape')
    parts=[multiply(x[...,i:i+256],matrix[:,i:i+256]) for i in range(0,1024,256)]
    result=parts[0]
    for part in parts[1:]:result=H(result+part)
    return result

def decoder_entry(x,skip,params):
    matrix,scale=params
    if skip.shape!=(x.shape[0]*2,x.shape[1]*2,512):raise ValueError('block39 skip shape')
    main=project(x,matrix)
    up=np.repeat(np.repeat(main,2,0),2,1)
    # No intermediate half rounding of skip*scale: one final half fused add.
    return F(H(up+skip*scale))
