"""Compare the continuous RGB encoder's raw block30 and padded head outputs."""
from pathlib import Path
import json
import numpy as np

root = Path('release/native-rgb-valid1080/amd-head')
checks = []
for name, count, target in (
    ('main', 60 * 36 * 512, 'oracle-main30-raw.f32'),
    ('down', 32 * 20 * 1024, 'oracle-head.f32'),
):
    actual = np.fromfile(root / f'gpu-{name}.f32', np.float32)
    expected = np.fromfile(root / target, np.float32)
    assert actual.size == expected.size == count
    assert np.isfinite(actual).all() and np.isfinite(expected).all()
    checks.append({'branch': name, 'values': count,
                   'different': int(np.count_nonzero(actual != expected)),
                   'max_abs': float(np.abs(actual - expected).max())})
result = {'scope': 'same controlled valid1080 RGB GPU0..30/head seed0; not full game texture contract',
          'checks': checks, 'pass': all(c['different'] == 0 for c in checks)}
(root / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
assert result['pass']
