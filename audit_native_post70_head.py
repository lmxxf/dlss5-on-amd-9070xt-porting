"""Inspect a candidate final FP16 head layout without fitting coefficients."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
root=Path('release/native-post70/smoke')
raw=np.fromfile(root/'weights.bin',np.uint8);assert raw.size==21808
tail=raw[0x5130:].view('<f2').astype(np.float32);assert tail.size==512
matrix=np.empty((16,32),np.float32)
matrix[bits(512,[2,5,6,7]),bits(512,[0,1,3,4,8])]=tail
report={'scope':'candidate FP16 output-head layout, not numerical proof',
        'head_offset':0x5130,'head_bytes':1024,
        'finite':bool(np.isfinite(matrix).all()),
        'nonzero_per_output_row':np.count_nonzero(matrix,axis=1).tolist(),
        'blend_half':float(np.fromfile(root/'blend.bin','<f2')[0])}
(root.parent/'head-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
