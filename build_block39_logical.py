#!/usr/bin/env python3
"""Build the portable logical matrix for decoder block39."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    values = np.fromfile(args.weights, dtype="<f2").astype(np.float32)
    if values.size != 262656:
        raise ValueError(f"expected 262656 FP16 values, got {values.size}")
    grouped = values[:262144].reshape(512, 512)
    depthwise_skip = values[262144:]
    matrix = np.zeros((1536, 512), dtype="<f4")
    matrix[:512, :256] = grouped[:256].T
    matrix[512:1024, 256:] = grouped[256:].T
    matrix[1024 + np.arange(512), np.arange(512)] = depthwise_skip
    matrix.tofile(args.output)
    document = {
        "file": args.output.name,
        "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "storage": "float32_little_endian",
        "shape": [1536, 512],
        "input_layout": [
            {"name": "decoder_main", "shape": [1024], "range": [0, 1024]},
            {"name": "block30_output1_skip", "shape": [512], "range": [1024, 1536]}
        ],
        "formula": "concat(main[:512] @ W[:256]^T, main[512:] @ W[256:]^T) + skip * depthwise_skip",
        "operation_graph": ["convolution", "convolution", "mul", "add"],
        "boundary": "archive logical semantics; original CUBIN numerical output is invalid without runtime weight packing"
    }
    args.manifest.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(document, indent=2))


if __name__ == "__main__":
    main()
