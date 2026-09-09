#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);a=p.parse_args();tiles=np.memmap(a.input,'<f4',mode='r',shape=(270,480,8,8,32));out=np.memmap(a.output,'<f4',mode='w+',shape=(2160,3840,32));out[:]=tiles.transpose(0,2,1,3,4).reshape(2160,3840,32);out.flush();print(f"shape={out.shape} finite={np.isfinite(out).all()} std={out.std():.7g}")
if __name__=='__main__':main()
