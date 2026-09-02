#!/usr/bin/env python3
"""Pack a fitted block70 attention checkpoint into a portable FP32 blob."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import torch


ORDER = ("Qe", "Qo", "Ke", "Ko", "Ve", "Vo", "P", "bias", "skip", "scale")


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
    arrays = [state[name].detach().numpy().astype("<f4") for name in ORDER]
    offsets: dict[str, dict[str, object]] = {}
    cursor = 0
    with args.output.open("wb") as stream:
        for name, array in zip(ORDER, arrays):
            raw = array.tobytes()
            offsets[name] = {
                "shape": list(array.shape),
                "byte_offset": cursor,
                "byte_size": len(raw),
            }
            stream.write(raw)
            cursor += len(raw)
    manifest = {
        "file": args.output.name,
        "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "storage": "float32_little_endian",
        "byte_size": cursor,
        "tensors": offsets,
        "formula": "((softmax(normalize(Q) @ normalize(K)^T * scale + bias) @ V) @ P + input * skip) * skip",
        "heldout": {
            "correlation": args.correlation,
            "mae": args.mae,
            "rmse": args.rmse,
        },
        "boundary": "attention-only oracle with exact NVIDIA five-operation prefix input",
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
