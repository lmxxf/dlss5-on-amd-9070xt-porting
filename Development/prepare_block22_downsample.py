#!/usr/bin/env python3
"""Build the block22 pool/256->512 matrices from its archive record."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

def main() -> None:
    p=argparse.ArgumentParser();p.add_argument("record",type=Path);p.add_argument("identity",type=Path);p.add_argument("enter",type=Path);a=p.parse_args()
    values=np.fromfile(a.record,dtype="<f2")
    if values.size!=410144:raise ValueError(values.size)
    matrix=values[344608:410144].astype(np.float32).reshape(256,256)
    np.eye(256,dtype=np.float32).tofile(a.identity)
    enter=np.empty((512,256),dtype=np.float32)
    enter[:256]=np.eye(256,dtype=np.float32)
    enter[256:]=matrix.T
    enter.tofile(a.enter)
    print(f"matrix={matrix.shape} enter={enter.shape} finite={np.isfinite(enter).all()} std={enter.std():.7g}")
if __name__=="__main__":main()
