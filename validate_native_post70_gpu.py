"""Independent final-RGB comparison for the isolated AMD post70 test."""
from pathlib import Path
import json,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group();g.add_argument('--after-rgb-fix',action='store_true');g.add_argument('--integer-head',action='store_true');a=p.parse_args()
root=Path('release/native-post70/amd');path=root/('gpu-integer-head.f32' if a.integer_head else 'gpu-after-rgb-fix.f32' if a.after_rgb_fix else 'gpu.f32')
got=np.fromfile(path,'<f4');expected=np.fromfile(root/'oracle.f32','<f4')
valid=got.size==expected.size==786432 and np.isfinite(got).all() and np.isfinite(expected).all()
different=int(np.count_nonzero(got!=expected)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','values':int(got.size),'different':different,
        'max_error':float(np.max(np.abs(got-expected))) if valid else None,
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'scope':'isolated AMD512 post70 mask1 mode1 final RGB; not full network/game'}
(root/('validation-integer-head.json' if a.integer_head else 'validation-after-rgb-fix.json' if a.after_rgb_fix else 'validation.json')).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','AMD post70 final RGB differs'
