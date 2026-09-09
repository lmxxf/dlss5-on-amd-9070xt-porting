"""Create a spatially distinct B input without changing seed or alpha."""
from pathlib import Path
import hashlib
import json
import numpy as np

root = Path('release/native-rgb-valid1080')
source = root / 'input-hwc.rgba32f'
a = np.fromfile(source, np.float32).reshape(1080, 1920, 4)
assert np.isfinite(a).all()
b = a.copy()
b[:, :, :3] = a[:, ::-1, :3]
assert np.array_equal(a[:, :, 3], b[:, :, 3])
assert not np.array_equal(a[:, :, :3], b[:, :, :3])
out = root / 'alternate'
out.mkdir(exist_ok=False)
b.tofile(out / 'input.f32')
(out / 'provenance.json').write_text(json.dumps({
    'source': str(source), 'operation': 'horizontal RGB reflection, alpha unchanged',
    'A_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'B_sha256': hashlib.sha256((out / 'input.f32').read_bytes()).hexdigest(),
    'scope': 'controlled dynamic-input test only; no B original oracle yet',
}, indent=2) + '\n')
print(out)
