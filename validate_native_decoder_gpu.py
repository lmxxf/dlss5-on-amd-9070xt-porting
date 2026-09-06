"""Independent readback comparison for the isolated AMD block39 fixture."""
from pathlib import Path
import argparse,json
import numpy as np
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args()
expected=np.fromfile(a.folder/'oracle.f32','<f4')
got=np.fromfile(a.folder/'gpu.f32','<f4')
valid=got.size==expected.size==131072 and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail',
        'scope':'isolated AMD decoder39, 8x8 main and 16x16 skip; not RGB chain or game',
        'values':int(got.size),'different':different,
        'max_error':float(np.max(np.abs(got-expected))) if valid else None}
(a.folder/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
assert report['status']=='pass','AMD block39 readback differs'
