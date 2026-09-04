#!/usr/bin/env python3
"""Decode finite E4M3 bytes into little-endian IEEE FP16."""
import argparse
import pathlib
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("input", type=pathlib.Path)
p.add_argument("output", type=pathlib.Path)
a = p.parse_args()
v = np.fromfile(a.input, dtype=np.uint8)
sign = np.where(v & 0x80, -1.0, 1.0)
exponent = (v >> 3) & 15
mantissa = v & 7
x = sign * (1.0 + mantissa / 8.0) * np.exp2(exponent.astype(np.int16) - 7)
subnormal = exponent == 0
x[subnormal] = sign[subnormal] * (mantissa[subnormal] / 8.0) * (2.0 ** -6)
x[(exponent == 15) & (mantissa == 7)] = np.nan
if not np.all(np.isfinite(x)):
    raise SystemExit("non-finite E4M3 weight")
x.astype("<f2").tofile(a.output)
print(f"elements={v.size} output_bytes={v.size * 2}")
