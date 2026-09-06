"""Diagnostic reduction candidates, NOT a validated attention implementation."""
from pathlib import Path
import argparse, json
import numpy as np
from native_c32_reference import H, F
from native_c32_softmax_sum import denominator
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn

p = argparse.ArgumentParser()
p.add_argument('folder', type=Path)
a = p.parse_args()
data = np.load(a.folder / 'logical.npz')
n = data['q'].shape[0]
q, k, v = [data[x].reshape(n, 32, 32).transpose(1, 0, 2) for x in ('q', 'k', 'v')]
score = H(q @ k.transpose(0, 2, 1))
coef = np.array([0x2dbb], np.uint16).view(np.float16).astype(np.float32)[0]
affine = np.clip(H(score * coef + np.float32(1.708984375)), 1.439453125, 1.9775390625)
b = affine.astype(np.float16).view(np.uint16).astype(np.uint32)
exp = (((b << 4) + 0x4000) & 65535).astype(np.uint16).view(np.float16).astype(np.float32)
num = np.zeros((32, n, 32), np.float32)
for start in range(0, n, 32):
    num = H(num + F(exp[..., start:start+32]) @ v[:, start:start+32])
order = np.zeros(64, np.int32)
for bit, dst in enumerate([4, 0, 1, 3, 2, 5]):
    order |= ((np.arange(64) >> bit) & 1) << dst
chunks = [exp[..., s:s+64][..., np.argsort(order)] for s in range(0, n, 64)]
den = denominator(chunks[0])
for chunk in chunks[1:]:
    den = H(den + denominator(chunk))
candidates = {'sum_chunk_denominators': den}
combined = chunks[0]
for chunk in chunks[1:]:
    combined = H(combined + chunk)
candidates['sum_positions_then_reduce'] = denominator(combined)
parity = []
for odd in (0, 1):
    partial = []
    for lane in (0, 2, 8, 10):
        value = np.zeros((32, n), np.float32)
        for chunk in chunks:
            for offset in (0, 4, 32, 36):
                base = odd + lane + offset
                value = H(value + H(chunk[..., base] + chunk[..., base+16]))
        partial.append(value)
    total = partial[0]
    for value in partial[1:]:
        total = H(total + value)
    parity.append(total)
candidates['accumulate_lane_pairs'] = H(parity[0] + parity[1])[..., None]
raw = np.fromfile(a.folder / 'rtx-output-1.fp8', np.uint8)
t = bits(n*1024, [2,6,7,8,14,15] + list(range(16,10+n.bit_length()-1)))
c = bits(n*1024, [0,1,3,4,5,9,10,11,12,13])
actual = np.empty((n,1024), np.float32)
actual[t,c] = e4m3fn(raw[:n*1024])
report = {'scope': 'diagnostic candidates only; no runtime changes', 'tokens': n, 'candidates': {}}
for name, den in candidates.items():
    expected = F(H(num * H(1/den))).transpose(1,0,2).reshape(n,1024)
    delta = np.abs(expected-actual)
    report['candidates'][name] = {'different': int(np.count_nonzero(delta)), 'max_abs': float(delta.max())}
print(json.dumps(report, indent=2))
(a.folder / 'candidate-validation.json').write_text(json.dumps(report, indent=2)+'\n')
