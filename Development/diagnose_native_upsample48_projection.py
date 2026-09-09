"""Read-only spatial/permutation checks on the failed identity projection."""
from pathlib import Path
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-upsample48/spatial-2401-0-projection-control')
x=np.fromfile(root/'input.f32','<f4').reshape(8,8,512)
inverse=np.argsort(np.load('release/native-c256/view/mapping.npz')['cell_output_to_hwc'])
raw=np.fromfile(root/'output.fp8',np.uint8)
y=e4m3fn(raw[:65536].reshape(-1,4096)[:,inverse]).reshape(4,4,4,4,256).transpose(0,2,1,3,4).reshape(16,16,256)
report={'repeated_2x2_differences':int(np.count_nonzero(y-np.repeat(np.repeat(y[::2,::2],2,0),2,1))),
        'sorted_first256_differences':int(np.count_nonzero(np.sort(y.ravel())-np.sort(np.repeat(x[:,:,:256].ravel(),4))))}
for name,v in [('first',x[:,:,:256]),('second',x[:,:,256:]),('even',x[:,:,::2]),('odd',x[:,:,1::2])]:
    report[name+'_different']=int(np.count_nonzero(y[::2,::2]!=v))
print(json.dumps(report,indent=2))
