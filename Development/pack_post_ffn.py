#!/usr/bin/env python3
"""Pack fitted block70 FFN parameters into a portable FP32 blob."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import torch


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--correlation", type=float, required=True)
    parser.add_argument("--mae", type=float, required=True)
    parser.add_argument("--rmse", type=float, required=True)
    args = parser.parse_args()
    state = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    order = ("W1", "W2", "skip")
    cursor = 0
    tensors: dict[str, dict[str, object]] = {}
    with args.output.open("wb") as stream:
        for name in order:
            array = state[name].detach().numpy().astype("<f4")
            raw = array.tobytes()
            tensors[name] = {"shape": list(array.shape), "byte_offset": cursor,
                             "byte_size": len(raw)}
            stream.write(raw)
            cursor += len(raw)
    manifest = {
        "file": args.output.name,
        "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "storage": "float32_little_endian",
        "byte_size": cursor,
        "tensors": tensors,
        "heldout": {"correlation": args.correlation, "mae": args.mae,
                    "rmse": args.rmse},
        "boundary": "FFN-only residual oracle with exact NVIDIA five-operation prefix input",
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
