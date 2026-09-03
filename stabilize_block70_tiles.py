#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("--limit",type=float,default=.03);p.add_argument("--ramp",type=int,default=8);a=p.parse_args();tiles=np.fromfile(a.input,'<f4').reshape(15,15,144,256,3);tiles=tiles-tiles.mean(axis=(2,3),keepdims=True);tiles=np.clip(tiles,-a.limit,a.limit);wx=np.ones(256,np.float32);wy=np.ones(144,np.float32);r=a.ramp;edge=np.sin(np.linspace(0,np.pi/2,r,endpoint=False,dtype=np.float32))**2;wx[:r]=edge;wx[-r:]=edge[::-1];wy[:r]=edge;wy[-r:]=edge[::-1];tiles*=wy[None,None,:,None,None]*wx[None,None,None,:,None];image=tiles.transpose(0,2,1,3,4).reshape(2160,3840,3);image.astype('<f4').tofile(a.output);print(f"finite={np.isfinite(image).all()} range={image.min():.7g}..{image.max():.7g} std={image.std():.7g}")
if __name__=='__main__':main()
