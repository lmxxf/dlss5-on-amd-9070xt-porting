#!/usr/bin/env python3
"""Recover block70's two-input 4x4x32 to 8x8x16 linear map."""

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


def e4m3(values: np.ndarray) -> np.ndarray:
    unsigned = values.astype(np.uint16)
    sign = np.where(unsigned & 0x80, -1.0, 1.0)
    exponent = (unsigned >> 3) & 15
    mantissa = unsigned & 7
    decoded = np.where(
        exponent == 0,
        sign * mantissa / 512.0,
        np.where(
            exponent == 15,
            np.nan,
            sign * (1.0 + mantissa / 8.0) * np.exp2(exponent.astype(int) - 7),
        ),
    )
    return np.nan_to_num(decoded, nan=0.0).astype(np.float32)


def input_vector(path: Path) -> np.ndarray:
    record = e4m3(np.fromfile(path, dtype=np.uint8))
    if record.size != 2560:
        raise ValueError(f"expected one 2560-byte record: {path}")
    return np.concatenate((record[:512], record[2048:2560]))


def odd_target(path: Path) -> np.ndarray:
    values = np.fromfile(path, dtype="<f4").reshape(64, 32)
    return values[:, :16].reshape(1024)


def metric(prediction: np.ndarray, target: np.ndarray) -> dict[str, float]:
    error = prediction - target
    return {
        "correlation": float(np.corrcoef(prediction, target)[0, 1]),
        "mae": float(np.mean(np.abs(error))),
        "rmse": float(np.sqrt(np.mean(error * error))),
        "max_abs_error": float(np.max(np.abs(error))),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("hadamard_output", type=Path)
    parser.add_argument("small_input", type=Path)
    parser.add_argument("small_output", type=Path)
    parser.add_argument("real_input", type=Path)
    parser.add_argument("real_output", type=Path)
    parser.add_argument("matrix", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()

    output = np.fromfile(args.hadamard_output, dtype="<f4").reshape(1024, 64, 32)
    output = output[:, :, :16].reshape(1024, 1024)
    matrix = (hadamard(1024).T @ output) / (1024 * 0.5)
    matrix.astype("<f4").tofile(args.matrix)
    small = metric(input_vector(args.small_input) @ matrix,
                   odd_target(args.small_output))
    real = metric(input_vector(args.real_input) @ matrix,
                  odd_target(args.real_output))
    manifest = {
        "file": args.matrix.name,
        "sha256": hashlib.sha256(args.matrix.read_bytes()).hexdigest(),
        "storage": "float32_little_endian",
        "shape": [1024, 1024],
        "input": "main[0:512] concatenated with skip[0:512], E4M3 decoded",
        "output": "8x8x16 pre-FFN values carried by the odd half of the 32-channel body",
        "recovery": "1024-row Rademacher Hadamard at +/-0.5",
        "small_heldout": small,
        "real_amplitude_heldout": real,
        "boundary": "portable effective input/upsample map; standard Swin body remains separate",
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
