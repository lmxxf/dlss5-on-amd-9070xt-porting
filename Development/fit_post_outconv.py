#!/usr/bin/env python3
"""Recover block70's portable 32-to-RGB head from controlled-weight output.

The post CUBIN stores the 3x32 matrix as two padded 3x16 tensor-core blocks.
`run_original_post_dataset ... 512 head` turns on one physical slot at a time
while preserving the complete shifted body.  This script
uses those responses as the feature basis, solves the effective matrix on one
tile, and validates it on an independent tile.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def active_indices(base: int, color: int) -> list[int]:
    return [base + color * 32 + (channel // 4) * 8 + channel % 4
            for channel in range(16)]


def load_basis(path: Path) -> np.ndarray:
    rgba = np.fromfile(path, dtype="<f4").reshape(512, 64, 4)
    residual = rgba[:, :, :3] - 0.5
    first = residual[active_indices(0, 0), :, 0].T
    second = residual[active_indices(256, 0), :, 0].T
    # The same physical basis must route independently to R, G and B.
    for base, reference in ((0, first), (256, second)):
        for color in range(3):
            candidate = residual[active_indices(base, color), :, color].T
            if not np.array_equal(candidate, reference):
                raise ValueError(f"RGB basis mismatch: base={base} color={color}")
    return np.concatenate((first, second), axis=1)


def load_target(path: Path) -> np.ndarray:
    return np.fromfile(path, dtype="<f4").reshape(64, 4)[:, :3] - 0.5


def metrics(prediction: np.ndarray, target: np.ndarray) -> dict[str, float]:
    error = prediction - target
    return {
        "correlation": float(np.corrcoef(prediction.ravel(), target.ravel())[0, 1]),
        "mae": float(np.mean(np.abs(error))),
        "rmse": float(np.sqrt(np.mean(error * error))),
        "max_abs_error": float(np.max(np.abs(error))),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("train_basis", type=Path)
    parser.add_argument("train_output", type=Path)
    parser.add_argument("heldout_basis", type=Path)
    parser.add_argument("heldout_output", type=Path)
    parser.add_argument("matrix", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()

    train_x = load_basis(args.train_basis)
    train_y = load_target(args.train_output)
    matrix = np.linalg.lstsq(train_x, train_y, rcond=1e-6)[0].astype("<f4")
    heldout_x = load_basis(args.heldout_basis)
    heldout_y = load_target(args.heldout_output)
    train_metrics = metrics(train_x @ matrix, train_y)
    heldout_metrics = metrics(heldout_x @ matrix, heldout_y)
    matrix.tofile(args.matrix)
    digest = hashlib.sha256(args.matrix.read_bytes()).hexdigest()
    manifest = {
        "file": args.matrix.name,
        "sha256": digest,
        "storage": "float32_little_endian",
        "shape": [32, 3],
        "physical_blocks": {
            "channels_0_15_base_half": 10392,
            "channels_16_31_base_half": 10648,
            "active_slot_formula": "base + color*32 + (channel//4)*8 + channel%4",
        },
        "train": train_metrics,
        "heldout": heldout_metrics,
        "boundary": "portable block70 RGB head; body feature recovery remains separate",
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
