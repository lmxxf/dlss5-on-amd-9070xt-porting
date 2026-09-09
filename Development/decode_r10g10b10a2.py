#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);a=p.parse_args();v=np.fromfile(a.input,'<u4').reshape(2160,3840);out=np.empty((2160,3840,4),np.float32);out[...,0]=(v&1023)/1023;out[...,1]=((v>>10)&1023)/1023;out[...,2]=((v>>20)&1023)/1023;out[...,3]=1;out.tofile(a.output);print(f"shape={out.shape} range={out[...,:3].min():.7g}..{out[...,:3].max():.7g}")
if __name__=='__main__':main()
