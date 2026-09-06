"""Audit SASS-ordered sampling against saved original register captures."""
from pathlib import Path
import json
import numpy as np
from native_temporal_sampling_reference import sample32
root=Path('release/native-temporal-holdout');checks=[]
history=np.fromfile(root/'history.f32',np.float32).reshape(8,8,4)[:,:,:3]
lane=np.arange(32)
for i,(dx,dy) in enumerate([(.37,-.29),(-.63,.81),(.015625,.484375),(.72,.23)]):
    capture=json.loads((root/f'case{i}/sample-registers.json').read_text())
    assert capture['pc_offset']==0x1800 and len({r['pc'] for r in capture['rows']})==1
    actual=np.array([r['rgb'] for r in capture['rows']],np.float32)
    expected=sample32(history,lane%8+.5+float(np.float32(dx)),lane//8+.5+float(np.float32(dy)))
    assert np.isfinite(expected).all() and np.isfinite(actual).all()
    checks.append({'case':i,'values':actual.size,'different_float32':int(np.count_nonzero(actual!=expected)),
                   'different_half':int(np.count_nonzero(actual.astype(np.float16)!=expected.astype(np.float16))),
                   'max_abs':float(np.abs(actual-expected).max())})
report={'scope':'four original warp0 sampler holdouts; half output only, not full temporal preblock or AMD',
        'checks':checks,'half_pass':all(r['different_half']==0 for r in checks)}
(root/'sample32-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
assert report['half_pass']
