#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);a=p.parse_args();tiles=np.fromfile(a.input,'<f4').reshape(15,15,144,256,3);image=tiles.transpose(0,2,1,3,4).reshape(2160,3840,3);image.tofile(a.output);print(f"shape={image.shape} finite={np.isfinite(image).all()} std={image.std():.7g}")
if __name__=='__main__':main()
