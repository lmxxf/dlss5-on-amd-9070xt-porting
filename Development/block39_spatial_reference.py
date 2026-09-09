#!/usr/bin/env python3
"""Fixed-frame spatial reference for decoder block39 (8x8 -> 16x16)."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def e4m3(values: np.ndarray) -> np.ndarray:
    unsigned = values.astype(np.uint16)
    sign = np.where(unsigned & 0x80, -1.0, 1.0)
    exponent = (unsigned >> 3) & 15
    mantissa = unsigned & 7
    result = np.where(exponent == 0, sign * mantissa / 512.0,
        np.where(exponent == 15, np.nan,
                 sign * (1 + mantissa / 8.0) * np.exp2(exponent.astype(int) - 7)))
    return np.nan_to_num(result, nan=0.0).astype(np.float32)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights", type=Path)
    parser.add_argument("main", type=Path, help="8x8x1024 canonical FP32")
    parser.add_argument("skip", type=Path, help="block30 unpooled physical E4M3")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    weights = np.fromfile(args.weights, dtype="<f2").astype(np.float32)
    main = np.fromfile(args.main, dtype="<f4").reshape(8, 8, 1024)
    skip = e4m3(np.fromfile(args.skip, dtype=np.uint8)[:16 * 16 * 512]).reshape(16, 16, 512)
    grouped = weights[:262144].reshape(512, 512)
    projected = np.concatenate((
        main[..., :512] @ grouped[:256].T,
        main[..., 512:] @ grouped[256:].T,
    ), axis=-1)
    upsampled = np.repeat(np.repeat(projected, 2, axis=0), 2, axis=1)
    output = upsampled + skip * weights[262144:]
    output.astype("<f4").tofile(args.output)
    print(f"finite={np.isfinite(output).all()} range={output.min():.9g}..{output.max():.9g} std={output.std():.9g}")


if __name__ == "__main__":
    main()
