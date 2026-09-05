"""Exact orthogonal probing recovery, not a fixed-image regression."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args()
y=np.fromfile(a.folder/'oracle.f32','<f4').reshape(2048,2048)
assert np.isfinite(y).all() and np.max(np.abs(y))<500
# In-place Walsh-Hadamard transform along input-probe dimension.
matrix=y.copy()
stride=1
while stride<2048:
 v=matrix.reshape(-1,2,stride,2048);left=v[:,0].copy();right=v[:,1].copy()
 v[:,0]=left+right;v[:,1]=left-right;stride*=2
matrix/=1024
matrix.astype('<f4').tofile(a.folder/'matrix.f32')
nz=np.abs(matrix)>1e-5
print(json.dumps({'shape':[2048,2048],'nonzero':int(nz.sum()),'zero_output_columns':int((~nz.any(0)).sum()),'max_coeff':float(np.abs(matrix).max())},indent=2))
