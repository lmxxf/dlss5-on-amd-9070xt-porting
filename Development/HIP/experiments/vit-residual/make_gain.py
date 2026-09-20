from pathlib import Path
import sys,json
import numpy as np
root=Path(sys.argv[1]);gain=np.ones(1024,dtype=np.float64);rows=[]
for block in range(31,39):
 for part in ['contract','projection']:
  p=root/f'block{block}-{part}.f32'
  if not p.exists():p=p.with_suffix('.f16')
  x=np.fromfile(p,'<f4' if p.suffix=='.f32' else '<f2').astype(np.float64)
  assert x.shape==(1024,) and np.isfinite(x).all();gain*=x
  rows.append(dict(block=block,part=part,min=float(x.min()),max=float(x.max()),mean=float(x.mean())))
assert np.isfinite(gain).all();gain.astype('<f4').tofile(root/'gain.f32')
(root/'gain-summary.json').write_text(json.dumps(dict(gain_min=float(gain.min()),gain_max=float(gain.max()),gain_mean=float(gain.mean()),coefficients=rows),indent=2)+'\n')
