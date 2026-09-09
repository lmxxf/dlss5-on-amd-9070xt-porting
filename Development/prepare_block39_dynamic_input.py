#!/usr/bin/env python3
"""Join dynamic ViT block38 main and encoder block30 skip for decoder block39."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("main",type=Path);p.add_argument("skip",type=Path);p.add_argument("matrix",type=Path);p.add_argument("output",type=Path);p.add_argument("matrix_with_bias",type=Path);a=p.parse_args()
 main=np.fromfile(a.main,dtype="<f4").reshape(36,60,1024)[:34]
 skip=np.fromfile(a.skip,dtype="<f4").reshape(68,120,512)
 up=np.repeat(np.repeat(main,2,axis=0),2,axis=1)
 np.concatenate((up,skip),axis=-1).astype("<f4").tofile(a.output)
 matrix=np.fromfile(a.matrix,dtype="<f4").reshape(1536,512)
 np.concatenate((matrix,np.zeros((1,512),np.float32)),axis=0).tofile(a.matrix_with_bias)
 print(f"main={main.shape} skip={skip.shape} input={(68,120,1536)} finite={np.isfinite(up).all() and np.isfinite(skip).all()}")
if __name__=="__main__":main()
