"""Audit measured half conversions against NumPy RNE and toward-zero values."""
from pathlib import Path
import json
import numpy as np

root=Path('release/half-conversion-probe')
report={}
for name in ['values','cast']:
    a=np.fromfile(root/f'half-conversion-{name}.f32',np.float32).reshape(-1,4)
    assert a.shape==(2048,4) and np.isfinite(a).all()
    v,soft,hardware=a[:,0],a[:,1],a[:,2]
    nearest=v.astype(np.float16)
    toward=nearest.copy()
    outside=np.abs(nearest.astype(np.float32))>np.abs(v)
    toward[outside]=np.nextafter(nearest[outside],np.float16(0))
    assert np.array_equal(soft,nearest.astype(np.float32))
    assert np.array_equal(hardware,toward.astype(np.float32))
    report[name]=dict(samples=len(v),software_rne_differences=0,
                      hardware_rne_differences=int(np.count_nonzero(hardware!=soft)),
                      hardware_toward_zero_differences=0)
report['scope']='Selected finite half midpoints/neighbors, current AMD preview driver and DXC; not a universal API rounding claim.'
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
