"""Independent readback comparison for isolated AMD block48."""
from pathlib import Path
import json,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('folder',type=Path,nargs='?',default=Path('release/native-upsample48/amd'));a=p.parse_args();root=a.folder
got=np.fromfile(root/'gpu.f32','<f4');expected=np.fromfile(root/'oracle.f32','<f4')
valid=got.size==expected.size and expected.size in (65536,262144) and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','values':int(got.size),'different':different,
        'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'sha256':hashlib.sha256((root/'gpu.f32').read_bytes()).hexdigest(),
        'scope':'isolated AMD block48; not RGB chain or game','output_extent':[int(np.sqrt(expected.size//256))]*2+[256]}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','AMD block48 differs'
