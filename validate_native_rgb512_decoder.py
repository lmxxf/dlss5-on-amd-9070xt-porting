"""Compare continuous AMD RGB512->decoder39 with the original chain output."""
from pathlib import Path
import json,hashlib
import numpy as np
root=Path('release/native-rgb512')
reference=json.loads((root/'decoder39-validation.json').read_text())
assert reference['status']=='pass' and reference['different']==0
expected=np.fromfile(root/'decoder39-oracle.f32','<f4')
path=root/'amd/output-rgb512-decoder39.f32'
got=np.fromfile(path,'<f4')
valid=got.size==expected.size==131072 and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','RGB_extent':[512,512],
        'last_block':39,'values':int(got.size),'different':different,
        'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'gpu_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'scope':'continuous AMD RGB512 through decoder39; not remaining decoder or game'}
(root/'amd/decoder39-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));assert report['status']=='pass','RGB512 decoder mismatch'
