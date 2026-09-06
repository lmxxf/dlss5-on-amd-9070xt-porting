"""Compare original plain and outview projection outputs in logical HWC."""
from pathlib import Path
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');count=16*16*512
plain=np.fromfile(root/'decoder-block47/output.fp8',np.uint8)
out=np.fromfile(root/'block47-outview.fp8',np.uint8)
assert not np.any(plain[count:]) and not np.any(out[count:])
assert not np.any((out[:count]&127)==127)
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
target=e4m3fn(plain[:count].reshape(-1,8192)[:,inverse]).reshape(4,4,4,4,512).transpose(0,2,1,3,4).reshape(16,16,512)
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
got=e4m3fn(out[:count]).reshape(32,16,16,16).transpose(1,2,0,3).reshape(16,16,512)[...,perm]
different=int(np.count_nonzero(got!=target))
report={'status':'pass' if different==0 else 'fail','different':different,'values':count,
        'scope':'original block47 outview matches plain in HWC; outview uses block32x4'}
(root/'block47-outview-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
assert different==0,'outview differs'
