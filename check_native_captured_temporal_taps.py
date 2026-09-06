"""Reconstruct from original captured TEX UVs/weights, not predicted coordinates."""
from pathlib import Path
import json,struct
import numpy as np
from native_temporal_sampling_reference import bilinear,fma32
root=Path('release/native-temporal-large');capture=root/'block14-warp1'
rows=json.loads((capture/'sample-registers-1590.json').read_text())['rows']
value=lambda row,k:struct.unpack('<f',struct.pack('<I',row['raw'][str(k)]))[0]
uv=np.array([[[value(r,x),value(r,y)] for x,y in [(48,49),(44,45),(54,45),(60,61),(56,45)]] for r in rows],np.float32)
xy=np.floor(uv.astype(np.float64)*2**21)/2**21*[120,72]
history=np.fromfile(root/'history.f32',np.float32).reshape(72,120,4)[:,:,:3]
pixels=bilinear(history,xy,8,8).astype(np.float32)
wy0,wx3,wx0,wy3,wxm,wym=np.array([[value(r,k) for k in (19,43,46,47,50,51)] for r in rows],np.float32).T
w=np.stack([wy0*wxm,wx0*wym,wxm*wym,wy3*wxm,wx3*wym],-1)
result=pixels[:,0]*w[:,0,None]
for i in range(1,5):result=fma32(pixels[:,i],w[:,i,None],result)
total=w[:,1]+w[:,0]
for i in range(2,5):total=total+w[:,i]
result=result*(np.float32(1)/total)[:,None]
actual=np.array([r['rgb'] for r in json.loads((capture/'sample-registers.json').read_text())['rows']],np.float32)
assert np.isfinite(actual).all() and np.isfinite(result).all()
report={'scope':'captured original UV/weights reconstruct same warp; not independent full sampler acceptance','half_different':int(np.count_nonzero(actual.astype(np.float16)!=result.astype(np.float16))),'max_abs':float(np.abs(actual-result).max())}
(capture/'captured-taps-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report);assert report['half_different']==0
