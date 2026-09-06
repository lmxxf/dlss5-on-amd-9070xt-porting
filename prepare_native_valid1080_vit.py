"""Prepare original ViT input from the same RGB head, checking both layouts."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn

base = Path('release/native-rgb-valid1080')
head = base / 'encoder-split/pool-head'
maps = Path('release/native-vit/repack640')
out = base / 'vit'
n = 640 * 1024
report = json.loads((head / 'validation.json').read_text())
assert report['main_exact'] and report['pool_exact'] and report['head_exact']
physical = np.fromfile(head / 'head.fp8', np.uint8)
forward = np.fromfile(maps / 'forward.i32', '<u4')
gather = np.fromfile(maps / 'hwc-to-vit.i32', '<u4')
assert physical.size >= n and forward.size == gather.size == n
assert np.array_equal(np.sort(forward), np.arange(n))
assert np.array_equal(np.sort(gather), np.arange(n))
packed = physical[:n][forward]
t = bits(n, [2, 6, 7, 8, 14, 15, 16, 17, 18, 19])
c = bits(n, [0, 1, 3, 4, 5, 9, 10, 11, 12, 13])
logical = np.empty((640, 1024), np.float32)
logical[t, c] = e4m3fn(packed)
expected = np.fromfile(head / 'head.f32', np.float32)[gather].reshape(640, 1024)
assert np.isfinite(logical).all() and np.array_equal(logical, expected)
out.mkdir(exist_ok=False)
# Original callers use padded allocation-sized fixtures.
buffer = np.zeros(4 * 1024 * 1024, np.uint8)
buffer[:n] = packed
buffer.tofile(out / 'input.fp8')
logical.tofile(out / 'input.f32')
for block in range(31, 39):
    folder = out / f'block{block}'
    folder.mkdir()
    for stage in ('expand', 'contract', 'qkv', 'projection'):
        source = Path('release/native-vit') / f'block{block}-{stage}.weights'
        (folder / f'{stage}.weights').write_bytes(source.read_bytes())
(out / 'provenance.json').write_text(json.dumps({
    'source': str(head / 'head.fp8'), 'tokens': 640,
    'original_repack_vs_gpu_logical_gather_exact': True,
    'scope': 'same controlled valid1080 RGB; original ViT execution pending',
}, indent=2) + '\n')
print(out)
