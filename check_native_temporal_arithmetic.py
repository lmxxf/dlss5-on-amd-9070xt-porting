"""Check isolated GPU arithmetic against float32/FMA reference, not game output."""
from pathlib import Path
import json
import numpy as np
from native_temporal_sampling_reference import fma32
root=Path('release/native-temporal-large')
actual=np.fromfile(root/'gpu-arithmetic.f32',np.float32).reshape(-1,16)
p=actual[:,:2].copy()
extent=np.array([120,72],np.float32)
inverse=np.float32(1)/extent
before=fma32(p,extent,-.5)
center=np.floor(before)+np.float32(.5)
t=np.clip(fma32(p,extent,-center),0,1)
left=np.maximum(center-1,np.float32(.5))
first=left*inverse
pixel=fma32(first,extent,0)
last=pixel*inverse
expected=np.concatenate([p,before,center,t,left,first,pixel,last],axis=1)
assert np.isfinite(actual).all()
names=['u','v','floor_x','floor_y','center_x','center_y','t_x','t_y',
       'left_x','left_y','first_x','first_y','pixel_x','pixel_y','last_x','last_y']
report={'scope':'isolated arithmetic float32 comparison; GPU input reused, not full sampler acceptance',
        'different_by_field':dict(zip(names,np.count_nonzero(actual.view(np.uint32)!=expected.view(np.uint32),axis=0).tolist())),
        'pixel593_gpu':[hex(int(x)) for x in actual[593].view(np.uint32)],
        'pixel593_reference':[hex(int(x)) for x in expected[593].view(np.uint32)]}
(root/'gpu-arithmetic-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
