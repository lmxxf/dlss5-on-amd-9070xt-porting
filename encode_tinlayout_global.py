#!/usr/bin/env python3
"""Encode a candidate HWC FP32 tensor into fused-Swin physical E4M3 cells."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
from decode_tinlayout_global import local_maps

def quantize(value: np.ndarray) -> np.ndarray:
    value=np.asarray(value,np.float32); sign=np.where(value<0,0x80,0).astype(np.uint8); a=np.abs(value)
    exponent=np.floor(np.log2(np.maximum(a,2.0**-9))).astype(np.int32)
    normal=exponent>=-6; encoded_exp=np.clip(exponent+7,1,15)
    mantissa=np.rint((a/np.exp2(exponent)-1)*8).astype(np.int32)
    carry=mantissa>=8; encoded_exp=np.minimum(encoded_exp+carry,15);mantissa=np.where(carry,0,mantissa)
    mantissa=np.clip(mantissa,0,7);mantissa=np.where((encoded_exp==15)&(mantissa>6),6,mantissa)
    normal_byte=((encoded_exp<<3)|mantissa).astype(np.uint8)
    sub=np.clip(np.rint(a*512),0,7).astype(np.uint8)
    out=sign|np.where(normal,normal_byte,sub);out[a==0]=0
    # E4M3FN finite saturation.
    bad=((out>>3)==15)&((out&7)==7);out[bad]=(out[bad]&0x80)|0x7e
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('input',type=Path);p.add_argument('permutation64',type=Path);p.add_argument('output',type=Path);p.add_argument('height',type=int);p.add_argument('width',type=int);p.add_argument('channels',type=int);a=p.parse_args()
    x=np.fromfile(a.input,'<f4').reshape(a.height,a.width,a.channels);pm=np.fromfile(a.permutation64,'<i4').reshape(64,64);maps=local_maps(pm,a.channels);rows=(a.height+3)//4;cols=(a.width+3)//4;cb=16*a.channels;physical=np.zeros(rows*cols*cb,np.uint8);q=quantize(x)
    for cy in range(rows):
        for cx in range(cols):
            cell=physical[(cy*cols+cx)*cb:(cy*cols+cx+1)*cb];block=np.zeros((4,4,a.channels),np.uint8);part=q[cy*4:min((cy+1)*4,a.height),cx*4:min((cx+1)*4,a.width)];block[:part.shape[0],:part.shape[1]]=part;cell[maps[cy&1,cx&1]]=block
    physical.tofile(a.output);print(f'bytes={len(physical)} nonzero={np.count_nonzero(physical)}')
if __name__=='__main__':main()
