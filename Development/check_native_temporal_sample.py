"""Compare captured original sample with candidate interpolation precision."""
from pathlib import Path
import json
import numpy as np
from native_temporal_sampling_reference import geometry,bilinear
root=Path('release/native-temporal-inputs-gates')
r=json.loads((root/'sample-registers.json').read_text())
actual=np.array([x['rgb'] for x in r['rows']],dtype=np.float32)
assert len({x['pc'] for x in r['rows']})==1 and np.isfinite(actual).all()
history=np.fromfile(root/'history.f32',np.float32).reshape(8,8,4)[:,:,:3]
lane=np.arange(32);xy,weight=geometry(lane%8+.625,lane//8+.625,8,8)
checks=[]
for bits,product_bits in ((None,None),(8,None),(8,8)):
    predicted=(bilinear(history,xy,bits,product_bits)*weight[...,None]).sum(-2)
    checks.append({'fraction_bits':bits,'product_bits':product_bits,'max_abs':float(np.abs(predicted-actual).max()),
                   'different_float32':int(np.count_nonzero(predicted.astype(np.float32)!=actual)),
                   'different_half':int(np.count_nonzero(predicted.astype(np.float16)!=actual.astype(np.float16)))})
report={'scope':'original warp0 controlled shift .125; candidate interpolation, not full temporal path acceptance','checks':checks}
(root/'sample-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
