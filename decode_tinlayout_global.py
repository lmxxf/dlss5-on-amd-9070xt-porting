#!/usr/bin/env python3
"""Evaluate candidate global fused-Swin E4M3 microcell-to-HWC layouts.

Only the 64-channel within-tile permutation is basis-proven. The head-specific
token-bit candidates below are diagnostic hypotheses until paired live
input/output captures close numerically through the next layer.
"""

from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

TOKEN_BIT_SOURCES_BY_CHANNELS = {
    32: (4, 0, 1, 3, 2, 5),
    64: (4, 0, 1, 3, 5, 2),
    128: (3, 0, 1, 4, 5, 2),
    256: (3, 1, 0, 4, 5, 2),
    512: (3, 1, 0, 4, 5, 2),
}


def e4m3fn(values: np.ndarray) -> np.ndarray:
    u = values.astype(np.uint8)
    sign = np.where(u & 0x80, -1.0, 1.0)
    exponent = (u >> 3) & 15
    mantissa = u & 7
    decoded = sign * np.where(
        exponent == 0,
        (mantissa / 8.0) * 2.0**-6,
        (1.0 + mantissa / 8.0) * np.exp2(exponent.astype(np.float32) - 7),
    )
    # Original fused kernels use SATFINITE; preserve finite evidence at NaN codes.
    decoded[(exponent == 15) & (mantissa == 7)] = sign[(exponent == 15) & (mantissa == 7)] * 448.0
    return decoded.astype(np.float32)


def local_maps(permutation64: np.ndarray, channels: int) -> np.ndarray:
    token_bit_sources = TOKEN_BIT_SOURCES_BY_CHANNELS[channels]
    result = np.empty((2, 2, 4, 4, channels), dtype=np.int32)
    for quadrant_y in range(2):
        for quadrant_x in range(2):
            entries: list[tuple[int, int, int, int]] = []
            for local_y in range(4):
                for local_x in range(4):
                    x = quadrant_x * 4 + local_x
                    y = quadrant_y * 4 + local_y
                    spatial_bits = (
                        x & 1, (x >> 1) & 1, (x >> 2) & 1,
                        y & 1, (y >> 1) & 1, (y >> 2) & 1,
                    )
                    token = sum(
                        spatial_bits[source] << bit
                        for bit, source in enumerate(token_bit_sources)
                    )
                    for channel in range(channels):
                        physical64 = int(permutation64[token, channel % 64])
                        entries.append((
                            (channel // 64) * 4096 + physical64,
                            local_y, local_x, channel,
                        ))
            entries.sort()
            for rank, (_, local_y, local_x, channel) in enumerate(entries):
                result[quadrant_y, quadrant_x, local_y, local_x, channel] = rank
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("physical", type=Path)
    parser.add_argument("permutation64", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("height", type=int)
    parser.add_argument("width", type=int)
    parser.add_argument("channels", type=int, choices=(32, 64, 128, 256, 512))
    args = parser.parse_args()
    physical = np.fromfile(args.physical, dtype=np.uint8)
    permutation64 = np.fromfile(args.permutation64, dtype="<i4").reshape(64, 64)
    maps = local_maps(permutation64, args.channels)
    rows = (args.height + 3) // 4
    columns = (args.width + 3) // 4
    cell_bytes = 16 * args.channels
    required = rows * columns * cell_bytes
    if physical.size < required:
        raise ValueError(f"need {required} bytes, got {physical.size}")
    output = np.empty((rows * 4, columns * 4, args.channels), dtype=np.float32)
    for cell_y in range(rows):
        for cell_x in range(columns):
            begin = (cell_y * columns + cell_x) * cell_bytes
            cell = physical[begin : begin + cell_bytes]
            output[
                cell_y * 4 : (cell_y + 1) * 4,
                cell_x * 4 : (cell_x + 1) * 4,
            ] = e4m3fn(cell[maps[cell_y & 1, cell_x & 1]])
    output = output[: args.height, : args.width]
    output.astype("<f4").tofile(args.output)
    horizontal = np.corrcoef(output[:, :-1].ravel(), output[:, 1:].ravel())[0, 1]
    vertical = np.corrcoef(output[:-1].ravel(), output[1:].ravel())[0, 1]
    print(
        f"candidate_token_bits={TOKEN_BIT_SOURCES_BY_CHANNELS[args.channels]} "
        f"shape={output.shape} finite={np.isfinite(output).all()} "
        f"range={output.min():.7g}..{output.max():.7g} std={output.std():.7g} "
        f"neighbor_h={horizontal:.7g} neighbor_v={vertical:.7g}"
    )


if __name__ == "__main__":
    main()
