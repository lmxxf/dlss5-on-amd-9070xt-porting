#!/usr/bin/env python3
"""Unpack PTX mma.m16n8k32 E4M3 matrix-B register fragments.

The mapping follows NVIDIA PTX ISA 9.1 section 9.7.14.5.10.  A 512-byte
physical subtile contains 32 lanes x 16 bytes; each lane contributes two
8-byte B fragments, producing two K32xN8 matrices.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

W1_OFFSETS = (0, 0x200, 0x400, 0x600, 0x1000, 0x1200, 0x1400, 0x1600)

def unpack_b_e4m3(tile: bytes) -> np.ndarray:
    if len(tile) != 512:
        raise ValueError(f"expected 512 bytes, got {len(tile)}")
    src = np.frombuffer(tile, dtype=np.uint8).reshape(32, 16)
    out = np.empty((2, 32, 8), dtype=np.uint8)
    for lane in range(32):
        group = lane >> 2
        thread = lane & 3
        for half in range(2):
            for i, value in enumerate(src[lane, half * 8 : half * 8 + 8]):
                row = thread * 4 + (i & 3) + (16 if i >= 4 else 0)
                out[half, row, group] = value
    return out

def pack_b_e4m3(matrices: np.ndarray) -> bytes:
    if matrices.shape != (2, 32, 8):
        raise ValueError(f"expected (2,32,8), got {matrices.shape}")
    out = np.empty((32, 16), dtype=np.uint8)
    for lane in range(32):
        group = lane >> 2
        thread = lane & 3
        for half in range(2):
            for i in range(8):
                row = thread * 4 + (i & 3) + (16 if i >= 4 else 0)
                out[lane, half * 8 + i] = matrices[half, row, group]
    return out.tobytes()

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    raw = args.weights.read_bytes()
    fragments = []
    for tid_y in range(2):
        row = []
        for offset in W1_OFFSETS:
            begin = tid_y * 0x2000 + offset
            physical = raw[begin : begin + 512]
            matrices = unpack_b_e4m3(physical)
            if pack_b_e4m3(matrices) != physical:
                raise AssertionError(f"roundtrip failed: tid_y={tid_y} offset={offset:#x}")
            row.append(matrices)
        fragments.append(row)
    np.savez(args.output, w1_b_fragments=np.asarray(fragments, dtype=np.uint8))
    print("shape: (2 tid_y, 8 offsets, 2 fragments, 32 K, 8 N)")
    print("roundtrip: 8192/8192 loaded bytes exact")
    print(f"wrote: {args.output}")

if __name__ == "__main__":
    main()
