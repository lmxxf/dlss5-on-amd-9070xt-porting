#!/usr/bin/env python3
"""Prepare block48 grouped 512->256 prefix and merge its encoder skip."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("weights",type=Path);p.add_argument("matrix",type=Path);p.add_argument("--projected",type=Path);p.add_argument("--skip",type=Path);p.add_argument("--output",type=Path);a=p.parse_args()
 v=np.fromfile(a.weights,dtype="<f2").astype(np.float32);prefix=v[:65536].reshape(256,256);matrix=np.zeros((513,256),np.float32);matrix[:256,:128]=prefix[:128].T;matrix[256:512,128:]=prefix[128:].T;matrix.tofile(a.matrix)
 if a.projected or a.skip or a.output:
  if not(a.projected and a.skip and a.output):raise ValueError("projected, skip and output are required together")
  projected=np.fromfile(a.projected,dtype="<f4").reshape(68,120,256);skip=np.fromfile(a.skip,dtype="<f4").reshape(136,240,256);merged=np.repeat(np.repeat(projected,2,axis=0),2,axis=1)+skip;merged.tofile(a.output);print(f"merged={merged.shape} finite={np.isfinite(merged).all()} std={merged.std():.7g}")
 else:print(f"matrix={matrix.shape} finite={np.isfinite(matrix).all()} std={matrix.std():.7g}")
if __name__=="__main__":main()
