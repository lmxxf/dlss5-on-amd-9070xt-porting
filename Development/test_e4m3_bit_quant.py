"""Numerical candidate check; does not replace a GPU shader comparison."""
import numpy as np

rng = np.random.default_rng(297)
bits = rng.integers(0, 0x7f800000, 2_000_000, dtype=np.uint32)
positive = bits.view(np.float32)
# Every representable E4M3 halfway boundary and its FP32 neighbours.
levels = np.unique(np.concatenate([
    np.arange(9, dtype=np.float32) / 512,
    *[(1 + np.arange(9, dtype=np.float32) / 8) * np.float32(2.0**e)
      for e in range(-6, 9)],
]))
mid = (levels[:-1] + levels[1:]) / 2
positive = np.concatenate([positive, mid,
    np.nextafter(mid, np.float32(0)),
    np.nextafter(mid, np.float32(np.inf))])
a = np.concatenate([positive, -positive])
m = np.abs(a)
exponent = np.clip(np.floor(np.log2(np.maximum(m, np.float32(2.0**-149)))), -6, 8)
power = np.exp2(exponent)
mantissa = np.rint((m / power - 1) * 8)
carry = mantissa >= 8
mantissa = np.where(carry, 0, mantissa)
exponent = exponent + carry
old = np.sign(a) * np.minimum(np.exp2(exponent) * (1 + mantissa / 8), 448)
small = np.rint(np.clip(a, -.015625, .015625) * 512) / 512
old = np.where(m < .015625, small, old)
b = np.minimum(m, np.float32(448)).astype(np.float32).view(np.uint32)
b = (b + 0x7ffff + ((b >> 20) & 1)) & np.uint32(0xfff00000)
new = np.copysign(b.view(np.float32), a)
new = np.where(m < .015625, small, new)
bad = np.flatnonzero(old != new)
print(f'finite samples={len(a)} mismatches={len(bad)}')
if len(bad):
    print(a[bad[:8]], old[bad[:8]], new[bad[:8]])
    raise SystemExit(1)
