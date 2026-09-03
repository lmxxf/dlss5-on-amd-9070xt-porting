#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("--threshold",type=float,default=1e-5);a=p.parse_args();w=np.fromfile(a.input,'<f4').reshape(1024,2048);offset=[0];indices=[];values=[]
 for o in range(2048):
  ids=np.flatnonzero(np.abs(w[:,o])>a.threshold);indices.extend(ids.tolist());values.extend(w[ids,o].tolist());offset.append(len(indices))
 with a.output.open('wb') as f:np.asarray(offset,'<u4').tofile(f);np.asarray(indices,'<u4').tofile(f);np.asarray(values,'<f4').tofile(f)
 print(f"outputs=2048 nnz={len(indices)} bytes={a.output.stat().st_size} max_per_output={np.diff(offset).max()}")
if __name__=='__main__':main()
