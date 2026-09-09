#!/usr/bin/env python3
"""Prepare identity pooling and 512->1024 projection matrices for block30."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("matrix",type=Path);p.add_argument("identity",type=Path);p.add_argument("enter",type=Path);a=p.parse_args()
 matrix=np.fromfile(a.matrix,dtype="<f4").reshape(512,1024)
 np.eye(512,dtype=np.float32).tofile(a.identity)
 matrix.T.astype(np.float32).tofile(a.enter)
 print(f"matrix={matrix.shape} finite={np.isfinite(matrix).all()} std={matrix.std():.7g}")
if __name__=="__main__":main()
