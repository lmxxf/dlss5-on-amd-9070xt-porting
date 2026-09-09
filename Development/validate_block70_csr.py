"""Validate the on-disk CSR against its original dense matrix and GPU output."""
import argparse
from pathlib import Path
import numpy as np
import json

p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--gpu',type=Path);a=p.parse_args()
dense=np.fromfile('block70-prefix-effective.bin','<f4').reshape(1024,2048)
raw=Path('block70-prefix-sparse.bin').read_bytes()
ends=np.frombuffer(raw,'<u4',2049);nnz=int(ends[-1]);offset=2049*4
assert len(raw)==offset+nnz*8
ids=np.frombuffer(raw,'<u4',nnz,offset=offset)
weights=np.frombuffer(raw,'<f4',nnz,offset=offset+nnz*4)
assert ends[0]==0 and np.all(ends[1:]>=ends[:-1]) and np.all(ids<1024) and np.isfinite(weights).all()
reconstructed=np.zeros_like(dense)
for o in range(2048):
 lo,hi=ends[o:o+2];reconstructed[ids[lo:hi],o]=weights[lo:hi]
thresholded=np.where(np.abs(dense)>1e-5,dense,0)
np.testing.assert_array_equal(reconstructed,thresholded)
# The oracle's main record is bank-major (2 x 4 x 4 x 16), not HWC32.
yy,xx,cc=np.indices((8,8,32))
main_indices=np.argmax(np.abs(dense[:512]),axis=0).reshape(8,8,32)
expected_indices=(cc//16)*256+((yy//2)*4+xx//2)*16+cc%16
np.testing.assert_array_equal(main_indices,expected_indices)
a.folder.mkdir(parents=True,exist_ok=True)
source=a.folder/'input.f32'
if not a.gpu:
 rng=np.random.default_rng(29770)
 samples=np.concatenate([np.eye(1024,dtype=np.float32),rng.uniform(-16,16,(32,1024)).astype(np.float32)])
 samples.astype('<f4').tofile(source)
 print(json.dumps({'tiles':len(samples),'csr_bytes':len(raw),'index_offset':offset,'nnz':nnz,'dense_thresholded_exact':True}))
else:
 samples=np.fromfile(source,'<f4').reshape(-1,1024)
 expected=samples@thresholded
 actual=np.fromfile(a.gpu,'<f4').reshape(expected.shape)
 delta=np.abs(actual-expected)
 report={'tiles':len(samples),'dense_thresholded_exact':True,'basis_max_error':float(delta[:1024].max()),'random_max_error':float(delta[1024:].max()),'mean_abs_error':float(delta.mean()),'nonfinite':int((~np.isfinite(actual)).sum())}
 print(json.dumps(report,indent=2))
 assert report['basis_max_error']==0 and report['random_max_error']<1e-4 and report['nonfinite']==0
