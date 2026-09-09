"""Numerical analysis of same-frame GPU readbacks; does not edit/render images."""
import argparse
import json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('folder', type=Path)
p.add_argument('--roi', nargs=4, type=int, metavar=('X', 'Y', 'W', 'H'))
a = p.parse_args()
m = json.loads((a.folder / 'metadata.json').read_text())
assert m['files_complete'] and m['fence_completed'] and m['mode'] == 0
w, h = m['width'], m['height']

def texture(name):
    d = np.fromfile(a.folder / name, '<u4').reshape(h, m['row_pitch'] // 4)[:, :w]
    return d, np.stack([(d >> s) & 1023 for s in (0, 10, 20)], -1).astype(np.float32)

before_bits, before = texture('before.r10')
after_bits, after = texture('after.r10')
back_bits, back = texture('backbuffer.r10')
body = np.memmap(a.folder / 'body.f32', '<f4', mode='r').reshape(-1, 32)[:w*h]
weights = np.fromfile(a.folder / 'weights.f32', '<f4').reshape(32, 3)
assert np.isfinite(weights).all()
assert np.any(before_bits), 'Invalid capture: actual game backbuffer is all zero'
assert np.any(weights), 'Invalid capture: known nonzero output weights read as zero'
residual = np.empty((w*h, 3), np.float32)
# Match the shader's sequential sum rather than BLAS reassociation.
for start in range(0, w*h, 65536):
    block = body[start:start+65536]
    assert np.isfinite(block).all()
    r = np.zeros((len(block), 3), np.float32)
    for i in range(32):
        r += block[:, i:i+1] * weights[i:i+1]
    residual[start:start+len(block)] = r
residual = residual.reshape(h, w, 3)
expected = np.rint(np.clip(before / 1023 + residual, 0, 1) * 1023)

def stats(b, out, r, predicted):
    delta = out-b
    return {
        'changed_pixel_fraction': float(np.mean(np.any(delta != 0, -1))),
        'mean_abs_change_8bit_levels': float(np.abs(delta).mean()*255/1023),
        'max_abs_change_8bit_levels': float(np.abs(delta).max()*255/1023),
        'abs_change_8bit_percentiles_50_90_99': (np.percentile(np.abs(delta), [50,90,99])*255/1023).tolist(),
        'residual_abs_mean': float(np.abs(r).mean()),
        'residual_abs_max': float(np.abs(r).max()),
        'residual_abs_percentiles_50_90_99': np.percentile(np.abs(r), [50,90,99]).tolist(),
        'predicted_changed_pixel_fraction': float(np.mean(np.any(predicted != b, -1))),
        'observed_vs_formula_max_10bit_levels': float(np.abs(out-predicted).max()),
        'observed_vs_formula_over_1level_fraction': float(np.mean(np.abs(out-predicted)>1)),
    }

result = {'metadata':m, 'weight_abs_max':float(np.abs(weights).max()),
          'body_abs_max':float(np.abs(body).max()),
          'backbuffer_exactly_matches_composite':bool(np.array_equal(back_bits,after_bits)),
          'whole_frame':stats(before,after,residual,expected)}
phases=residual[:h//8*8,:w//8*8].reshape(h//8,8,w//8,8,3)
phase_mean=phases.mean(axis=(0,2),keepdims=True)
content_mse=float(np.mean((phases-phase_mean)**2))
total_variance=float(np.var(phases,axis=(0,1,2,3)).mean())
result['periodic_8x8']={'variance_explained_by_phase':1-content_mse/total_variance if total_variance else 0,
                        'phase_rgb_mean':phase_mean.reshape(8,8,3).tolist()}
if a.roi:
    x,y,rw,rh=a.roi
    assert 0<=x<x+rw<=w and 0<=y<y+rh<=h
    s=np.s_[y:y+rh,x:x+rw]
    result['roi']={'xywh':a.roi, **stats(before[s],after[s],residual[s],expected[s])}
print(json.dumps(result, indent=2))
