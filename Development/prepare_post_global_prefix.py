#!/usr/bin/env python3
"""Gather block70 physical global views into portable per-tile records."""

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
    parser.add_argument("main", type=Path)
    parser.add_argument("skip", type=Path)
    parser.add_argument("main_records", type=Path)
    parser.add_argument("skip_records", type=Path)
    parser.add_argument("--combine-main", type=Path)
    parser.add_argument("--combine-skip", type=Path)
    parser.add_argument("--combined-output", type=Path)
    args = parser.parse_args()
    main = e4m3(np.fromfile(args.main, dtype=np.uint8))
    skip = e4m3(np.fromfile(args.skip, dtype=np.uint8))
    main_records, skip_records = [], []
    main_bank = 18 * 8192
    for tile_y in range(18):
        for tile_x in range(32):
            base = tile_y * 8192 + tile_x * 64
            main_records.append(np.concatenate(
                [main[base + plane * 2048:base + plane * 2048 + 64]
                 for plane in range(4)] +
                [main[main_bank + base + plane * 2048:
                      main_bank + base + plane * 2048 + 64]
                 for plane in range(4)]))
            base = tile_y * 65536 + tile_x * 1024
            skip_records.append(np.concatenate(
                (skip[base:base + 1024], skip[base + 32768:base + 33792])))
    np.asarray(main_records, dtype="<f4").tofile(args.main_records)
    np.asarray(skip_records, dtype="<f4").tofile(args.skip_records)
    if args.combine_main and args.combine_skip and args.combined_output:
        first = np.fromfile(args.combine_main, dtype="<f4").reshape(18, 32, 8, 8, 32)
        second = np.fromfile(args.combine_skip, dtype="<f4").reshape(18, 32, 8, 8, 32)
        (first + second).transpose(0, 2, 1, 3, 4).reshape(144, 256, 32).tofile(
            args.combined_output)


if __name__ == "__main__":
    main()
