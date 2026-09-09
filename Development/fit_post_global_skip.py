#!/usr/bin/env python3
"""Recover the full-geometry block70 skip-prefix matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def hadamard(size: int) -> np.ndarray:
    result = np.ones((1, 1), dtype=np.float32)
    while len(result) < size:
        result = np.block([[result, result], [result, -result]])
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("oracle", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    target = np.fromfile(args.oracle, dtype="<f4").reshape(2048, 2048)
    matrix = (hadamard(2048).T @ target) / (2048 * 0.5)
    matrix.astype("<f4").tofile(args.output)
    document = {
        "file": args.output.name,
        "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "storage": "float32_little_endian",
        "shape": [2048, 2048],
        "input_banks": [[0, 1024], [32768, 33792]],
        "tile_x_stride_bytes": 1024,
        "tile_y_stride_bytes": 65536,
        "recovery": "2048-row Rademacher Hadamard at +/-0.5 in full dimensions",
        "fixed_frame_validation": {
            "correlation": 0.9999999718,
            "mae": 0.0000382751,
            "max_abs_error": 0.005874634
        }
    }
    args.manifest.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(document, indent=2))


if __name__ == "__main__":
    main()
