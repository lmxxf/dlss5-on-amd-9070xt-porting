#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);a=p.parse_args();w=np.fromfile(a.input,'<f4');assert w.size==32963
 first=w[:19008].reshape(32,66,3,3);first[:,64:66]=0;w.tofile(a.output);print(f"zeroed={32*2*3*3} remaining_nonzero={np.count_nonzero(w)}")
if __name__=='__main__':main()
