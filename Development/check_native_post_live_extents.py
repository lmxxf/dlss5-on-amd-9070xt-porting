"""Check captured post scalars against the separately validated AMD output.

Uses fixed temporal fixtures, not game frames or a live history feedback loop.
"""
from pathlib import Path
import hashlib
import json
import numpy as np

root = Path('release/native-temporal-valid1080/post70')

def read(name, shape):
    path = root / name
    assert path.stat().st_size == int(np.prod(shape)) * 4, name
    return np.memmap(path, np.float32, 'r', shape=shape)

shift = read('origin-minus4-full.f32', (1152, 1920, 4))
word70 = read('word70-one.f32', (1152, 1920, 4))
live = read('live-extents.f32', (1080, 1920, 4))
gpu = read('shift-full-gpu.f32', (1152, 1920, 3))
checks = []
for label, expected, actual in (
    ('word70_0_vs_1', shift, word70),
    ('valid1080_vs_processing_crop', shift[:1080], live),
    ('original_live_extents_vs_amd_crop', live[:, :, :3], gpu[:1080]),
):
    checks.append(dict(name=label, values=actual.size,
                       different=int(np.count_nonzero(expected != actual)),
                       max_abs=float(np.abs(expected - actual).max()),
                       finite=bool(np.isfinite(expected).all() and np.isfinite(actual).all())))
report = dict(scope='fixed temporal fixture; mask1/rgb1; origin(-4,-4); valid1080; word70=1',
              game_verified=False, checks=checks,
              live_rgb_sha256=hashlib.sha256(live[:, :, :3].copy().tobytes()).hexdigest())
(root / 'live-extents-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
assert all(c['different'] == 0 and c['finite'] for c in checks)
