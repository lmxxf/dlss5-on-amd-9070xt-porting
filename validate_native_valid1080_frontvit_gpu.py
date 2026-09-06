"""Check the direct RGB encoder -> GPU gather -> ViT outputs."""
from pathlib import Path
import json
import numpy as np

root = Path('release/native-rgb-valid1080/amd-frontvit')
checks = []
for actual_name, expected_name in [('gpu-main.f32', 'oracle-vit.f32'),
                                   ('gpu-down.f32', 'oracle-head.f32')]:
    actual = np.fromfile(root / actual_name, np.float32)
    expected = np.fromfile(root / expected_name, np.float32)
    assert actual.size == expected.size == 655360
    assert np.isfinite(actual).all() and np.isfinite(expected).all()
    checks.append({'output': actual_name, 'values': actual.size,
                   'different': int(np.count_nonzero(actual != expected)),
                   'max_abs': float(np.abs(actual - expected).max())})
report = {'scope': 'same valid1080 RGB direct GPU0..38 seed0; not full game contract',
          'checks': checks, 'pass': all(c['different'] == 0 for c in checks)}
(root / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
assert report['pass']
