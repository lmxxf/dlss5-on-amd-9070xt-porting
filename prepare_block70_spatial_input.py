#!/usr/bin/env python3
"""Prepare the fixed-frame 66-channel input for the AMD block70 CNN."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

def main() -> None:
    p=argparse.ArgumentParser()
    p.add_argument("block69",type=Path,help="H1088xW1920xC32 AMD output")
    p.add_argument("block0",type=Path,help="H1088xW1920xC32 AMD output")
    p.add_argument("block69_correction",type=Path,help="33x32 affine matrix")
    p.add_argument("output",type=Path)
    p.add_argument("--full",action="store_true",help="prepare the complete 3840x2176 tensor")
    a=p.parse_args()
    matrix=np.fromfile(a.block69_correction,dtype="<f4").reshape(33,32)
    if a.full:
        b69=np.memmap(a.block69,dtype="<f4",mode="r",shape=(1088,1920,32))
        b0=np.memmap(a.block0,dtype="<f4",mode="r",shape=(1088,1920,32))
        output=np.memmap(a.output,dtype="<f4",mode="w+",shape=(2176,3840,66))
        xcoord=(np.arange(3840,dtype=np.float32)/3839)[:,None]
        ones=np.ones((1920,1),np.float32)
        for sy in range(1088):
            corrected=np.concatenate((np.asarray(b69[sy]),ones),axis=1)@matrix
            main=np.repeat(corrected,2,axis=0);skip=np.repeat(np.asarray(b0[sy]),2,axis=0)
            for oy in (sy*2,sy*2+1):
                row=np.concatenate((main,skip,xcoord,np.full((3840,1),oy/2175,np.float32)),axis=1)
                output[oy]=row
        output.flush()
        print(f"shape={output.shape} bytes={output.nbytes} output={a.output}")
        return
    b69=np.memmap(a.block69,dtype="<f4",mode="r",shape=(1088,1920,32))[288:360,1152:1280]
    flat=np.asarray(b69).reshape(-1,32)
    b69=(np.c_[flat,np.ones(len(flat),np.float32)]@matrix).reshape(72,128,32)
    b69=np.repeat(np.repeat(b69,2,axis=0),2,axis=1)
    b0=np.memmap(a.block0,dtype="<f4",mode="r",shape=(1088,1920,32))[288:360,1152:1280]
    b0=np.repeat(np.repeat(np.asarray(b0),2,axis=0),2,axis=1)
    y,x=np.indices((144,256))
    output=np.concatenate((b69,b0,(x/255).astype(np.float32)[...,None],
                           (y/143).astype(np.float32)[...,None]),axis=2)
    output.astype("<f4").tofile(a.output)
    print(f"shape={output.shape} bytes={output.nbytes} output={a.output}")

if __name__=="__main__":main()
