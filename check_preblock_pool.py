"""Isolate DS rounding with identity branches and independently known FP16 mix."""
import json
import os
import subprocess
from pathlib import Path
import numpy as np
from preblock_mix_reference import inputs, unpack_mix
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize

folder = Path('release/preblock-pool-control')
folder.mkdir(parents=True, exist_ok=True)
original = np.fromfile('/tmp/block0.weights', '<f2')
weights = np.zeros_like(original)
weights[4104:4616] = original[4104:4616]
weights[4616:4648] = 1
weights[10808:10840] = 1
weights.view('<f4')[10288//2] = 1
weights.tofile(folder/'identity.weights')
source = Path('release/preblock-branch-audit/input.rgba32f')
environment = {key: value for key, value in os.environ.items() if not key.startswith('DLSS5_PREBLOCK_')}
environment.update(DLSS5_PREBLOCK_WIDTH='8', DLSS5_PREBLOCK_HEIGHT='8', DLSS5_PREBLOCK_SEED='0x3f800000')
subprocess.run(['/tmp/preblock-branch-oracle', '/tmp/dlssnr-cubins/dlssnr-00.cubin',
                str(folder/'identity.weights'), str(source), str(folder/'main.fp8'),
                str(folder/'ds.fp8'), '0', '0'], check=True, env=environment)
half = lambda x: np.asarray(x, np.float16).astype(np.float32)
fp8 = lambda x: e4m3fn(quantize(x))
rgb = np.fromfile(source, '<f4').reshape(-1, 8, 8, 4)
raw = half(np.stack([inputs(tile[:, :, :3]) @ unpack_mix(original).T for tile in rgb]))
mapping = np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32', '<f4').reshape(2048, 2048)), axis=0)
main = e4m3fn(np.fromfile(folder/'main.fp8', np.uint8).reshape(-1, 2048)[:, mapping]).reshape(raw.shape)
assert np.array_equal(fp8(raw), main), 'input isolation failed'
y, x, c = np.indices((4, 4, 32))
index = (c//16)*256+(y*4+x)*16+c%16
target = e4m3fn(np.fromfile(folder/'ds.fp8', np.uint8).reshape(-1, 512)[:, index])
a, b, c, d = raw[:, ::2, ::2], raw[:, ::2, 1::2], raw[:, 1::2, ::2], raw[:, 1::2, 1::2]
candidates = {'float_mean': half((a+b+c+d)*.25),
              'horizontal_half': half(half(half(a+b)+half(c+d))*.25),
              'vertical_half': half(half(half(a+c)+half(b+d))*.25),
              'diagonal_half': half(half(half(a+d)+half(b+c))*.25)}
report = {}
for name, value in candidates.items():
    got = fp8(value)
    report[name] = dict(exact_fraction=float(np.mean(got == target)),
                        mae=float(np.abs(got-target).mean()), max_error=float(np.abs(got-target).max()))
print(json.dumps(report, indent=2))
assert np.array_equal(fp8(candidates['horizontal_half']), target), 'DS arithmetic regression'
