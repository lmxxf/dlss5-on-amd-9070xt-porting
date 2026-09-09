#!/usr/bin/env python3
"""Decode distilled pre-block physical 8x8 tiles into canonical HWC FP32."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from decode_tinlayout_global import e4m3fn, local_maps
from encode_tinlayout_global import quantize


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("permutation64", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("height", type=int)
    parser.add_argument("width", type=int)
    args = parser.parse_args()
    if args.height % 8 or args.width % 8:
        raise ValueError("height and width must be divisible by 8")
    tile_rows, tile_columns = args.height // 8, args.width // 8
    tile_count = tile_rows * tile_columns
    physical_f32 = np.memmap(args.input, dtype="<f4", mode="r", shape=(tile_count, 2048))
    permutation = np.fromfile(args.permutation64, dtype="<i4").reshape(64, 64)
    maps = local_maps(permutation, 32)
    output = np.memmap(args.output, dtype="<f4", mode="w+", shape=(args.height, args.width, 32))
    for tile_y in range(tile_rows):
        for tile_x in range(tile_columns):
            tile = quantize(physical_f32[tile_y * tile_columns + tile_x])
            for quadrant_y in range(2):
                for quadrant_x in range(2):
                    cell_index = quadrant_y * 2 + quadrant_x
                    cell = tile[cell_index * 512 : (cell_index + 1) * 512]
                    decoded = e4m3fn(cell[maps[quadrant_y, quadrant_x]])
                    y = tile_y * 8 + quadrant_y * 4
                    x = tile_x * 8 + quadrant_x * 4
                    output[y : y + 4, x : x + 4] = decoded
    output.flush()
    horizontal = np.corrcoef(output[:, :-1].reshape(-1), output[:, 1:].reshape(-1))[0, 1]
    vertical = np.corrcoef(output[:-1].reshape(-1), output[1:].reshape(-1))[0, 1]
    print(
        f"shape={output.shape} finite={np.isfinite(output).all()} "
        f"range={output.min():.7g}..{output.max():.7g} std={output.std():.7g} "
        f"neighbor_h={horizontal:.7g} neighbor_v={vertical:.7g}"
    )


if __name__ == "__main__":
    main()
