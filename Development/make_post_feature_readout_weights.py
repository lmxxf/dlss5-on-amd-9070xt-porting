#!/usr/bin/env python3
"""Build one controlled block70 RGB readout for three body channels."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("weights", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("first_channel", type=int)
    p.add_argument("sign", choices=("positive", "negative"))
    a = p.parse_args()
    if not 0 <= a.first_channel <= 29:
        raise ValueError("first_channel must be 0..29")
    w = np.fromfile(a.weights, dtype="<u2")
    if w.size != 10904:
        raise ValueError(f"expected 10904 FP16 slots, got {w.size}")
    w[10392:] = 0
    value = np.uint16(0x1400 if a.sign == "positive" else 0x9400)
    for color, channel in enumerate(range(a.first_channel, a.first_channel + 3)):
        block = 10392 if channel < 16 else 10648
        local = channel & 15
        slot = block + color * 32 + (local // 4) * 8 + local % 4
        w[slot] = value
    w.tofile(a.output)
    print(f"channels={a.first_channel}..{a.first_channel + 2} sign={a.sign} bytes={w.nbytes}")

if __name__ == "__main__":
    main()
