"""Compare isolated AMD decoder40..47 readback with original final output."""
from pathlib import Path
import json,hashlib
import numpy as np
root=Path('release/native-rgb512/amd-decoder40-47')
got=np.fromfile(root/'gpu-0.f32','<f4');expected=np.fromfile(root/'oracle-0.f32','<f4')
valid=got.size==expected.size==131072 and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','values':int(got.size),'different':different,
        'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'sha256':hashlib.sha256((root/'gpu-0.f32').read_bytes()).hexdigest(),
        'scope':'isolated AMD decoder40..47 with original decoder39 input; not full RGB/game'}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));assert report['status']=='pass','AMD decoder split mismatch'
