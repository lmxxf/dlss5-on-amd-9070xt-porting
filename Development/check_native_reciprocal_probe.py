"""Independent CUDA reciprocal instruction vs original captured normalization."""
from pathlib import Path
import json,subprocess
import numpy as np
root=Path('release/native-reciprocal');root.mkdir(exist_ok=True)
cap=Path('release/native-temporal-valid1080/block11-row9-warp1')
rows=json.loads((cap/'sample-registers-1590.json').read_text())['rows']
raw=np.array([[r['raw'][str(k)] for k in (19,43,46,47,50,51)] for r in rows],np.uint32).view(np.float32)
wy0,wx3,wx0,wy3,wxm,wym=raw.T
w=np.stack([wy0*wxm,wx0*wym,wxm*wym,wy3*wxm,wx3*wym],-1)
total=w[:,1]+w[:,0]
for i in range(2,5):total=total+w[:,i]
original=np.array([r['raw']['64'] for r in json.loads((cap/'sample-registers-16b0.json').read_text())['rows']],np.uint32)
values=np.r_[total,np.random.default_rng(7309).uniform(.9,1.2,1000000).astype(np.float32)].astype(np.float32)
values.tofile(root/'input.f32')
subprocess.run(['/tmp/probe-cuda-reciprocal',str(root/'input.f32'),str(root/'output.f32')],check=True,timeout=20)
actual=np.fromfile(root/'output.f32',np.uint32)
rounded=(np.float32(1)/values).view(np.uint32)
delta=actual.astype(np.int64)-rounded.astype(np.int64)
keys,counts=np.unique(delta,return_counts=True)
report={'scope':'independent rcp.approx instruction, one original captured warp plus random holdout; not AMD implementation',
 'captured_different':int(np.count_nonzero(actual[:32]!=original)),
 'ulp_delta_histogram':dict(zip(map(str,keys),map(int,counts)))}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
assert report['captured_different']==0
