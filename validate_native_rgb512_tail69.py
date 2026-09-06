"""Validate resident RGB512 through69, excluding output head/game acceptance."""
from pathlib import Path
import json,hashlib
import numpy as np
root=Path('release/native-rgb512');source=root/'decoder-block69'
assert json.loads((source/'validation.json').read_text())['status']=='pass'
path=root/'amd/output-rgb512-block69.f32';got=np.fromfile(path,'<f4');expected=np.fromfile(source/'oracle.f32','<f4')
valid=got.size==expected.size==2097152 and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','last_block':69,'RGB_extent':[512,512],
        'values':int(got.size),'different':different,'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'scope':'AMD RGB512 to69; not output head or game'}
(root/'amd/tail69-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','resident RGB512 to69 mismatch'
