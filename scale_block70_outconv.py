#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("scale",type=float);a=p.parse_args();w=np.fromfile(a.input,'<f4');assert w.size==96;(w*a.scale).astype('<f4').tofile(a.output);print(f"scale={a.scale} max={np.abs(w*a.scale).max():.7g}")
if __name__=='__main__':main()
