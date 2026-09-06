"""Validate full GPU output; never substitute intermediate success for RGB."""
from pathlib import Path
import json
import numpy as np

root = Path('release/native-rgb-valid1080/amd-full')
checks = []
for name, target, count in [('gpu-main.f32', 'oracle-final.f32', 1920*1152*3),
                            ('gpu-down.f32', 'oracle-head.f32', 655360)]:
    a = np.fromfile(root/name, np.float32)
    b = np.fromfile(root/target, np.float32)
    assert a.size == b.size == count and np.isfinite(a).all() and np.isfinite(b).all()
    checks.append({'output': name, 'values': count,
                   'different': int(np.count_nonzero(a != b)),
                   'max_abs': float(np.abs(a-b).max())})
report = {'scope': 'same valid1080 RGB direct GPU0..70 seed0, reflected post base; not actual game acceptance',
          'checks': checks, 'pass': all(c['different'] == 0 for c in checks)}
(root/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
assert report['pass']
