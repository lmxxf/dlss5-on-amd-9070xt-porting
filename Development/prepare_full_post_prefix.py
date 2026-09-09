"""Pack full-bank post prefix; no truncated 512-value skip fallback."""
import argparse,json
from pathlib import Path
import numpy as np
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('skip_matrix',type=Path);p.add_argument('--compare',action='store_true');a=p.parse_args()
main=np.fromfile('block70-prefix-effective.bin','<f4').reshape(1024,2048)[:512]
skip=np.fromfile(a.skip_matrix,'<f4').reshape(2048,2048)
matrix=np.concatenate([main,skip]);matrix=np.where(np.abs(matrix)>1e-5,matrix,0)
if not a.compare:
 a.folder.mkdir(parents=True,exist_ok=True)
 ends=[0];indices=[];values=[]
 for o in range(2048):
  ix=np.flatnonzero(matrix[:,o]);indices.extend(ix);values.extend(matrix[ix,o]);ends.append(len(indices))
 with (a.folder/'prefix.csr').open('wb') as f:
  np.asarray(ends,'<u4').tofile(f);np.asarray(indices,'<u4').tofile(f);np.asarray(values,'<f4').tofile(f)
 rng=np.random.default_rng(29772);raw=quantize(rng.uniform(-4,4,(64,2560)).astype(np.float32))
 raw.tofile(a.folder/'records.u8');e4m3fn(raw).tofile(a.folder/'input.f32')
 print(json.dumps({'input_dimensions':2560,'output_dimensions':2048,'nnz':len(indices),'records':64}))
else:
 x=np.fromfile(a.folder/'input.f32','<f4').reshape(-1,2560);y=np.fromfile(a.folder/'oracle.f32','<f4').reshape(-1,2048)
 predicted=x@matrix;e=predicted-y
 report={'correlation':float(np.corrcoef(predicted.ravel(),y.ravel())[0,1]),'mae':float(np.abs(e).mean()),'max_error':float(np.abs(e).max())}
 print(json.dumps(report,indent=2))
 assert np.isfinite(y).all() and report['correlation']>0.9999 and report['mae']<0.002
