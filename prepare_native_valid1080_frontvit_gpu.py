"""Prepare the direct GPU RGB0..38 chain without intermediate input fixtures."""
from pathlib import Path
import json

base = Path('release/native-rgb-valid1080')
assert json.loads((base / 'amd-head/validation.json').read_text())['pass']
report = json.loads((base / 'vit/validation.json').read_text())
assert len(report['stages']) == 56 and all(
    r['different'] == 0 and r['finite'] and r['replay_identical']
    for r in report['stages'])
out = base / 'amd-frontvit'
out.mkdir(exist_ok=False)
for src in (base / 'amd-head').glob('block*.f32'):
    (out / src.name).write_bytes(src.read_bytes())
for name in ('input.f32', 'head-matrix.f32', 'oracle-head.f32'):
    (out / name).write_bytes((base / 'amd-head' / name).read_bytes())
for block in range(31, 39):
    for stage in ('expand', 'contract', 'qkv', 'projection'):
        (out / f'block{block}-{stage}.f32').write_bytes(
            (base / f'vit/block{block}/{stage}.f32').read_bytes())
(out / 'oracle-vit.f32').write_bytes((base / 'vit/oracle.f32').read_bytes())
(out / 'hwc-to-vit.i32').write_bytes(Path('release/native-vit/repack640/hwc-to-vit.i32').read_bytes())
(out / 'provenance.json').write_text(json.dumps({
    'scope': 'same valid1080 RGB direct GPU0..38; execution pending',
    'input': 'input.f32 is the only image/feature input; head and ViT oracles are comparison-only',
}, indent=2) + '\n')
