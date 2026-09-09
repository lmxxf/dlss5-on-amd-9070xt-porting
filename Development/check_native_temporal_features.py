"""Compare current/temporal feature placement before original shared stores."""
from pathlib import Path
import json
import numpy as np
from preblock_mix_reference import inputs
from native_temporal_sampling_reference import sample32
root=Path('release/native-temporal-inputs-gates')
capture=json.loads((root/'sample-registers-1bd0.json').read_text())
assert capture['pc_offset']==0x1bd0
physical=np.array([[r['raw'][str(k)] for k in (4,5,6,7,48,49,50,51)] for r in capture['rows']],np.uint32).view(np.float16).astype(np.float32)
# Shared-store register grouping versus the existing mixing matrix feature order.
order=[0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15]
actual=np.empty_like(physical);actual[:,order]=physical
rgb=np.fromfile(root/'rgb.f32',np.float32).reshape(8,8,4)[:,:,:3]
history=np.fromfile(root/'history.f32',np.float32).reshape(8,8,4)[:,:,:3]
y,x=np.mgrid[:8,:8];sampled=sample32(history,x+.625,y+.625)
expected=inputs(rgb,0,True,temporal_rgb=sampled).reshape(-1,16)[:32]
channels=[i for i in range(16) if i not in (0,1,4)]
report={'scope':'warp0 feature packing; noise channels excluded because this helper uses approximate noise, not production function table',
        'temporal_feature_indices':[13,2,3],
        'non_noise_values':32*len(channels),
        'different':int(np.count_nonzero(actual[:,channels]!=expected[:,channels]))}
(root/'feature-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert report['different']==0
