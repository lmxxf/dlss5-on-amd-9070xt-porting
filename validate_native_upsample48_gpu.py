"""Independent readback comparison for isolated AMD block48."""
from pathlib import Path
import json,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('folder',type=Path,nargs='?',default=Path('release/native-upsample48/amd'));a=p.parse_args();root=a.folder
got=np.fromfile(root/'gpu.f32','<f4');expected=np.fromfile(root/'oracle.f32','<f4')
channels=json.loads((root/'provenance.json').read_text()).get('channels',256)
valid=got.size==expected.size and ((channels==256 and expected.size in (65536,262144)) or (channels==128 and expected.size==524288) or (channels==64 and expected.size==1048576) or (channels==32 and expected.size==2097152)) and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','values':int(got.size),'different':different,
        'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'sha256':hashlib.sha256((root/'gpu.f32').read_bytes()).hexdigest(),
        'scope':'isolated AMD native upsample; not RGB chain or game','output_extent':[int(np.sqrt(expected.size//channels))]*2+[channels]}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','AMD block48 differs'
