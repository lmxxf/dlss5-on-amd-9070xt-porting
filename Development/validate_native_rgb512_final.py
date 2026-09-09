"""Validate full resident RGB512->RGB against original CUBIN final output."""
from pathlib import Path
import json,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--decoder-root',type=Path,default=Path('release/native-rgb512'));p.add_argument('--encoder-root',type=Path,default=Path('release/native-rgb512'));a=p.parse_args()
root=a.decoder_root;assert json.loads((root/'post70/validation.json').read_text())['status']=='pass'
path=root/'amd/output-rgb512-final.f32';got=np.fromfile(path,'<f4');target=np.fromfile(root/'post70/oracle.f32','<f4')
valid=got.size==target.size==786432 and np.isfinite(got).all() and np.isfinite(target).all()
different=int(np.count_nonzero(got!=target)) if valid else None
report={'status':'pass' if valid and different==0 else 'fail','values':int(got.size),'different':different,'max_error':float(np.max(abs(got-target))) if valid else None,
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'scope':'full resident RGB512->RGB mask1 mode1; not1080p/game contract acceptance'}
if valid:
    color=np.fromfile(a.encoder_root/'input-hwc.rgba32f','<f4').reshape(-1,4)[:,:3].ravel()
    report['RGB_components_changed_gt_1e_5']=int(np.count_nonzero(abs(got-color)>1e-5))
(root/'amd/final-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['status']=='pass','full RGB512 final differs'
