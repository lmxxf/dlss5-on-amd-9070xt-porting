#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
from PIL import Image
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("backbuffer",type=Path);p.add_argument("neural",type=Path);p.add_argument("output",type=Path);p.add_argument("--intensity",type=float,default=.3);p.add_argument("--raw-output",type=Path);a=p.parse_args();packed=np.fromfile(a.backbuffer,'<u4').reshape(2160,3840);base=np.stack((packed&1023,(packed>>10)&1023,(packed>>20)&1023),axis=-1).astype(np.float32)/1023;neural=np.fromfile(a.neural,'<f4').reshape(2160,3840,3);result=base+neural*a.intensity
 if a.raw_output:result.astype('<f4').tofile(a.raw_output)
 Image.fromarray(np.rint(np.clip(result,0,1)*255).astype(np.uint8),'RGB').save(a.output);print(f"base_std={base.std():.7g} neural_std={neural.std():.7g} result_std={result.std():.7g}")
if __name__=='__main__':main()
