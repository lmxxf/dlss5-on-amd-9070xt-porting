#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("height",type=int);p.add_argument("padded_height",type=int);p.add_argument("width",type=int);p.add_argument("channels",type=int);a=p.parse_args()
 x=np.fromfile(a.input,dtype="<f4").reshape(a.height,a.width,a.channels);y=np.zeros((a.padded_height,a.width,a.channels),np.float32);y[:a.height]=x;y.tofile(a.output);print(f"{x.shape}->{y.shape}")
if __name__=="__main__":main()
