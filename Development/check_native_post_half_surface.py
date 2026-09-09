"""Compare original post FLOAT versus HALF surface using identical inputs."""
from pathlib import Path
import json
import numpy as np
root=Path('release/native-temporal-valid1080/post70')
a=np.fromfile(root/'live-extents.f32',np.float32)
b=np.fromfile(root/'live-half-surface.f32',np.float32)
assert a.size==b.size==1920*1080*4
assert np.isfinite(a).all() and np.isfinite(b).all() and (a>=0).all() and (a<=1).all()
nearest=a.astype(np.float16)
bits=nearest.view(np.uint16).copy()
bits[nearest.astype(np.float32)>a]-=1
truncated=bits.view(np.float16).astype(np.float32)
report=dict(values=a.size,nearest_different=int(np.count_nonzero(nearest.astype(np.float32)!=b)),
            truncate_different=int(np.count_nonzero(truncated!=b)),
            scope='original CUDA post surface conversion, fixed nonnegative1080p fixture; not live captured pixels')
print(report)
(root/'half-surface-validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['truncate_different']==0 and report['nearest_different']>0
