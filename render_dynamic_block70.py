#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
from PIL import Image
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("--width",type=int,default=3840);p.add_argument("--height",type=int,default=2176);a=p.parse_args();rgb=np.fromfile(a.input,dtype="<f4").reshape(a.height,a.width,3)[:2160];rgb=np.clip(rgb,0,1);Image.fromarray(np.rint(rgb*255).astype(np.uint8),"RGB").save(a.output);print(a.output)
if __name__=="__main__":main()
