#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("--width",type=int,default=3840);p.add_argument("--height",type=int,default=2160);a=p.parse_args();values=np.fromfile(a.input,dtype="<f4");pixels=a.height*a.width;channels=values.size//pixels
 if channels not in (3,4) or values.size!=pixels*channels:raise ValueError(f"unexpected element count {values.size}")
 rgb=values.reshape(a.height,a.width,channels)[...,:3];q=np.rint(np.clip(rgb,0,1)*1023).astype(np.uint32);packed=q[...,0]|(q[...,1]<<10)|(q[...,2]<<20)|(3<<30);packed.astype('<u4').tofile(a.output);print(f"bytes={packed.nbytes} channels={channels} min=0x{packed.min():08x} max=0x{packed.max():08x}")
if __name__=='__main__':main()
