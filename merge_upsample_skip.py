#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("projected",type=Path);p.add_argument("skip",type=Path);p.add_argument("output",type=Path);p.add_argument("height",type=int);p.add_argument("width",type=int);p.add_argument("channels",type=int);a=p.parse_args();x=np.fromfile(a.projected,'<f4').reshape(a.height,a.width,a.channels);skip=np.fromfile(a.skip,'<f4').reshape(a.height*2,a.width*2,a.channels);out=np.repeat(np.repeat(x,2,0),2,1)+skip;out.tofile(a.output);print(f"shape={out.shape} finite={np.isfinite(out).all()} std={out.std():.7g}")
if __name__=='__main__':main()
