"""Validate resident RGB512 through block48, not final game output."""
from pathlib import Path
import json,hashlib
import numpy as np
root=Path('release/native-rgb512');source=root/'upsample48-shift0'
assert json.loads((source/'validation.json').read_text())['status']=='pass'
path=root/'amd/output-rgb512-upsample48.f32'
got=np.fromfile(path,'<f4');expected=np.fromfile(source/'oracle.f32','<f4')
valid=got.size==expected.size==262144 and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','last_block':48,'RGB_extent':[512,512],
        'values':int(got.size),'different':different,'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'scope':'AMD RGB512 to48; not final RGB or game'}
(root/'amd/upsample48-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','resident RGB512 to48 mismatch'
