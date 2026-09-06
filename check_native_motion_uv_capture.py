"""Verify captured original UV against GPU motion output in one known warp."""
from pathlib import Path
import json
import numpy as np
root=Path('release/native-temporal-large')
actual=np.fromfile(root/'motionfma/gpu-temporal-coordinates.f32',np.uint32).reshape(72,120,2)[4:8,112:120].reshape(32,2)
rows=json.loads((root/'block14-warp1/sample-registers-10d0.json').read_text())['rows']
expected=np.array([[row['raw'][str(k)] for k in (46,19)] for row in rows],np.uint32)
report={'scope':'GPU motion UV vs original PC10d0 block14 warp1 only',
        'values':64,'different':int(np.count_nonzero(actual!=expected))}
(root/'motionfma/uv-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(report)
assert np.isfinite(actual.view(np.float32)).all() and report['different']==0
