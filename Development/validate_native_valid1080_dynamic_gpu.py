"""Audit restored A and B causality; B is not numerically certified by this test."""
from pathlib import Path
import json
import numpy as np

base = Path('release/native-rgb-valid1080')
root = base / 'amd-dynamic'
checks = []
for name, oracle, count in [
    ('gpu-main.f32', base/'post70/oracle.f32', 1920*1152*3),
    ('gpu-down.f32', base/'encoder-split/pool-head/head.f32', 655360),
    ('gpu-raw.f32', base/'decoder-c32/decoder-block69/oracle.f32', 960*576*32),
]:
    a = np.fromfile(root/name, np.float32)
    b = np.fromfile(root/(name+'.alternate'), np.float32)
    reference = np.fromfile(oracle, np.float32)
    assert a.size == b.size == reference.size == count
    assert np.isfinite(a).all() and np.isfinite(b).all() and np.isfinite(reference).all()
    checks.append({'output': name, 'values': count,
                   'restored_A_different_from_original': int(np.count_nonzero(a != reference)),
                   'B_different_from_A': int(np.count_nonzero(a != b))})
report = {'scope': 'GPU fixed-seed A/A/B/A/A: restored A exact, B neural causality only; not B numerical or game acceptance',
          'checks': checks,
          'pass': all(c['restored_A_different_from_original'] == 0 and c['B_different_from_A'] > 0 for c in checks)}
(root/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
assert report['pass']
