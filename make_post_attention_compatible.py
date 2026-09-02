#!/usr/bin/env python3
"""Convert block70 attention parameters to d3d12_block1_test layout."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("attention", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    values = np.fromfile(args.attention, dtype="<f4")
    tensors = json.loads(args.manifest.read_text(encoding="utf-8"))["tensors"]

    def get(name: str) -> np.ndarray:
        item = tensors[name]
        begin = item["byte_offset"] // 4
        end = (item["byte_offset"] + item["byte_size"]) // 4
        return values[begin:end].reshape(item["shape"])

    result = np.zeros(10305, dtype="<f4")
    for name, begin in (("Qe", 4096), ("Qo", 4352), ("Ke", 4608),
                        ("Ko", 4864), ("Ve", 5120), ("Vo", 5376)):
        result[begin:begin + 256] = get(name).ravel()
    shared_skip = get("skip")
    result[5632:6144] = (get("P") * shared_skip[:, None]).ravel()
    result[6144:10240] = get("bias").ravel()
    result[10240] = get("scale")[0]
    result[10241:10273] = 1.0
    result[10273:10305] = shared_skip * shared_skip
    result.tofile(args.output)
    print(f"wrote {result.nbytes} bytes: {args.output}")


if __name__ == "__main__":
    main()
