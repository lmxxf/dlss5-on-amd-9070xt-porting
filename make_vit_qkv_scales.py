#!/usr/bin/env python3
"""Reproduce the portable ViT Q/K median row-norm scales."""
import argparse
import pathlib
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("qkv_fp8", type=pathlib.Path)
p.add_argument("output_f32", type=pathlib.Path)
a = p.parse_args()
v = np.fromfile(a.qkv_fp8, dtype=np.uint8)
if v.size != 1024 * 3 * 1024:
    raise SystemExit(f"unexpected QKV size: {v.size}")
sign = np.where(v & 0x80, -1.0, 1.0)
exponent = (v >> 3) & 15
mantissa = v & 7
w = sign * (1.0 + mantissa / 8.0) * np.exp2(exponent.astype(np.int16) - 7)
subnormal = exponent == 0
w[subnormal] = sign[subnormal] * (mantissa[subnormal] / 8.0) * (2.0 ** -6)
w = w.reshape(1024, 3, 32, 32)
scales = np.empty((2, 32), dtype=np.float32)
for group in range(2):
    norms = np.sqrt(np.sum(w[:, group] ** 2, axis=-1))
    scales[group] = np.partition(norms, 512, axis=0)[512]
scales.astype("<f4").tofile(a.output_f32)
print(f"scales={scales.size} min={scales.min():.9g} max={scales.max():.9g}")
