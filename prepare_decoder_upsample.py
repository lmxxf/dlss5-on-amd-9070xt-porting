#!/usr/bin/env python3
"""Build grouped decoder projection and merge its 2x encoder skip."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("weights",type=Path);p.add_argument("matrix",type=Path);p.add_argument("main_channels",type=int);p.add_argument("output_channels",type=int);p.add_argument("--projected",type=Path);p.add_argument("--height",type=int);p.add_argument("--width",type=int);p.add_argument("--skip",type=Path);p.add_argument("--output",type=Path);a=p.parse_args()
 v=np.fromfile(a.weights,dtype="<f2").astype(np.float32);half_in=a.main_channels//2;half_out=a.output_channels//2;count=2*half_in*half_out;prefix=v[:count].reshape(a.output_channels,half_in);matrix=np.zeros((a.main_channels+1,a.output_channels),np.float32);matrix[:half_in,:half_out]=prefix[:half_out].T;matrix[half_in:a.main_channels,half_out:]=prefix[half_out:].T;matrix.tofile(a.matrix)
 if a.projected:
  if not(a.height and a.width and a.skip and a.output):raise ValueError("merge arguments incomplete")
  x=np.fromfile(a.projected,dtype="<f4").reshape(a.height,a.width,a.output_channels);skip=np.fromfile(a.skip,dtype="<f4").reshape(a.height*2,a.width*2,a.output_channels);y=np.repeat(np.repeat(x,2,axis=0),2,axis=1)+skip;y.tofile(a.output);print(f"merged={y.shape} finite={np.isfinite(y).all()} std={y.std():.7g}")
 else:print(f"matrix={matrix.shape} prefix_elements={count} ignored_prefix_tail={max(0,v.size-count)}")
if __name__=="__main__":main()
