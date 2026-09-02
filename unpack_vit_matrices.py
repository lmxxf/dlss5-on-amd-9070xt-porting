#!/usr/bin/env python3
"""Unswizzle DLSSNR ViT packed-E4M3 matrices into portable FP16 tensors."""

import argparse
import json
from pathlib import Path

import numpy as np


def e4m3_to_float(values: np.ndarray) -> np.ndarray:
    values = np.asarray(values, dtype=np.uint8)
    sign = np.where(values & 0x80, -1.0, 1.0)
    exponent = (values >> 3) & 15
    mantissa = values & 7
    decoded = sign * np.where(
        exponent == 0,
        mantissa / 8.0 * 2.0**-6,
        (1.0 + mantissa / 8.0) * np.exp2(exponent.astype(np.int16) - 7),
    )
    decoded[(exponent == 15) & (mantissa == 7)] = np.nan
    return decoded.astype(np.float32)


def axis_permutation(size: int, low_bit_targets: tuple[int, ...]) -> np.ndarray:
    bits = size.bit_length() - 1
    if 1 << bits != size:
        raise ValueError(f"axis size is not a power of two: {size}")
    targets = list(low_bit_targets) + [1 << bit for bit in range(5, bits)]
    result = np.zeros(size, dtype=np.int32)
    for logical in range(size):
        physical = 0
        for bit, target in enumerate(targets):
            if logical & (1 << bit):
                physical |= target
        result[logical] = physical
    if np.unique(result).size != size:
        raise AssertionError("axis permutation is not bijective")
    return result


# Kernel physical-channel index -> raw matrix axis index.  These are fixed
# matrix input/output layouts, not persistent names for an activation buffer.
MATRIX_INPUT_TO_RAW = (1, 2, 16, 4, 8)
MATRIX_OUTPUT_TO_RAW = (1, 8, 16, 2, 4)


def unpack_matrix(raw: bytes, input_size: int, output_size: int,
                  input_layout: str, output_layout: str) -> np.ndarray:
    expected = input_size * output_size
    if len(raw) != expected:
        raise ValueError(f"matrix bytes {len(raw)} != {expected}")
    lanes = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 32, 16)
    fragments = np.empty((lanes.shape[0], 2, 32, 8), dtype=np.uint8)
    for lane in range(32):
        group, thread = lane >> 2, lane & 3
        for half in range(2):
            for index in range(8):
                k = thread * 4 + (index & 3) + (16 if index >= 4 else 0)
                fragments[:, half, k, group] = lanes[:, lane, half * 8 + index]
    matrix = fragments.reshape(input_size // 32, output_size // 8, 32, 8)
    matrix = matrix.transpose(0, 2, 1, 3).reshape(input_size, output_size)
    layouts = {"matrix_input": MATRIX_INPUT_TO_RAW,
               "matrix_output": MATRIX_OUTPUT_TO_RAW}
    input_order = axis_permutation(input_size, layouts[input_layout])
    output_order = axis_permutation(output_size, layouts[output_layout])
    return e4m3_to_float(matrix[input_order][:, output_order]).astype(np.float16)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("arena", type=Path)
    parser.add_argument("index", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--blocks", default="31-38")
    args = parser.parse_args()
    first, last = map(int, args.blocks.split("-"))
    arena = args.arena.read_bytes()
    records = json.loads(args.index.read_text())
    if isinstance(records, dict):
        records = records["records"]
    records = {record["name"]: record for record in records}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {"storage": "float16_little_endian", "blocks": {}}
    for block in range(first, last + 1):
        outputs = {}
        specifications = {
            "expand": (0, 1024, 4096, "matrix_input", "matrix_output"),
            "contract": (1, 4096, 1024, "matrix_input", "matrix_output"),
        }
        for kind, (layer, inputs, outputs_count, input_layout,
                   output_layout) in specifications.items():
            record = records[f"block{block}.layer{layer}.layer"]
            begin = record["arena_offset"]
            matrix_bytes = inputs * outputs_count
            matrix = unpack_matrix(
                arena[begin:begin + matrix_bytes], inputs, outputs_count,
                input_layout, output_layout)
            filename = f"block{block}-vit-{kind}.f16"
            matrix.tofile(args.output_dir / filename)
            outputs[kind] = {
                "file": filename,
                "shape": [inputs, outputs_count],
                "input_layout": input_layout,
                "output_layout": output_layout,
            }
        manifest["blocks"][str(block)] = outputs
    (args.output_dir / "vit-matrices.json").write_text(
        json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
