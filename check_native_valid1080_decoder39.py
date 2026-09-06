"""Same-RGB original ViT38 + encoder30 skip -> decoder39, with layout checks."""
from pathlib import Path
import json, os, subprocess
import numpy as np
from decode_tinlayout_global import e4m3fn
from native_c32_reference import F, H
from native_decoder_entry_reference import unpack, project

base = Path('release/native-rgb-valid1080')
vit = base / 'vit'
report = json.loads((vit / 'validation.json').read_text())
assert len(report['stages']) == 56 and all(
    r['different'] == 0 and r['finite'] and r['tail_zero'] and r['replay_identical']
    for r in report['stages'])
root = base / 'decoder39'
n = 640 * 1024
inverse = np.fromfile('release/native-vit/repack640/inverse.i32', '<u4')
gather = np.fromfile('release/native-vit/repack640/vit-to-hwc.i32', '<u4')
assert inverse.size == gather.size == n
inv = np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
source = np.fromfile(vit / 'block38/trial-1-projection.fp8', np.uint8)
mainraw = source[:n][inverse]
main = e4m3fn(mainraw.reshape(-1, 2, 8192)[:, :, inv]).reshape(5, 8, 2, 4, 4, 512).transpose(0, 3, 1, 4, 2, 5).reshape(20, 32, 1024)
logical = np.fromfile(vit / 'oracle.f32', np.float32)
assert np.array_equal(main.ravel(), logical[gather])
skipraw = np.fromfile(base / 'encoder-split/pool-head/main.fp8', np.uint8)
count = 36 * 60 * 512
skip = e4m3fn(skipraw[:count].reshape(-1, 8192)[:, inv]).reshape(9, 15, 4, 4, 512).transpose(0, 2, 1, 3, 4).reshape(36, 60, 512)
assert np.isfinite(main).all() and np.isfinite(skip).all()
root.mkdir(exist_ok=False)
mainraw.tofile(root / 'main.fp8')
skipraw[:count].tofile(root / 'skip.fp8')
main.tofile(root / 'input.f32')
skip.tofile(root / 'residual.f32')
weights = root / 'weights.bin'
weights.write_bytes(Path('release/native-decoder-game/weights.bin').read_bytes())
subprocess.run(['/tmp/native-decoder-entry-oracle', '/tmp/dlssnr-cubins/dlssnr-06.cubin',
                str(root / 'main.fp8'), str(root / 'skip.fp8'), str(weights),
                str(root / 'result'), '32', '20', '--run'],
               env=dict(os.environ, DLSS5_DECODER_GAME_EXTENT='1'), check=True, timeout=20)
raw = np.fromfile(root / 'result.output.fp8', np.uint8)
actual = e4m3fn(raw[:count].reshape(-1, 8192)[:, inv]).reshape(9, 15, 4, 4, 512).transpose(0, 2, 1, 3, 4).reshape(skip.shape)
matrix, scale = unpack(weights)
up = np.repeat(np.repeat(project(main, matrix), 2, 0), 2, 1)[:36, :60]
expected = F(H(up + skip * scale))
result = {'scope': 'same valid1080 RGB original decoder39; AMD pending',
          'different': int(np.count_nonzero(actual != expected)),
          'finite': bool(np.isfinite(actual).all()), 'tail_zero': not bool(raw[count:].any()),
          'inverse_layout_exact': True}
(root / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
assert result['different'] == 0 and result['finite'] and result['tail_zero']
actual.tofile(root / 'oracle.f32')
np.concatenate([matrix.ravel(), scale]).astype('<f4').tofile(root / 'weights.f32')
