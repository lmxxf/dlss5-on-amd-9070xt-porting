#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("main",type=Path);p.add_argument("skip",type=Path);p.add_argument("output",type=Path);a=p.parse_args();main=np.memmap(a.main,'<f4',mode='r',shape=(1088,1920,32))[:1080];skip=np.memmap(a.skip,'<f4',mode='r',shape=(1088,1920,32))[:1080]
 def tiles(x):return np.asarray(x).reshape(270,4,480,4,32).transpose(0,2,1,3,4).reshape(129600,512)
 out=np.memmap(a.output,'<f4',mode='w+',shape=(129600,1024));out[:,:512]=tiles(main);out[:,512:]=tiles(skip);out.flush();print(f"shape={out.shape} bytes={out.nbytes} finite={np.isfinite(out).all()}")
if __name__=='__main__':main()
