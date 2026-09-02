#!/usr/bin/env python3
"""Merge portable post FFN and attention into d3d12_block1_test layout."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def tensor(blob: np.ndarray, manifest: dict, name: str) -> np.ndarray:
    item = manifest["tensors"][name]
    begin = item["byte_offset"] // 4
    end = (item["byte_offset"] + item["byte_size"]) // 4
    return blob[begin:end].reshape(item["shape"])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("ffn", type=Path)
    parser.add_argument("ffn_manifest", type=Path)
    parser.add_argument("attention", type=Path)
    parser.add_argument("attention_manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    ffn = np.fromfile(args.ffn, dtype="<f4")
    fm = json.loads(args.ffn_manifest.read_text(encoding="utf-8"))
    attention = np.fromfile(args.attention, dtype="<f4")
    am = json.loads(args.attention_manifest.read_text(encoding="utf-8"))
    result = np.zeros(10305, dtype="<f4")
    result[:2048] = tensor(ffn, fm, "W1").ravel()
    result[2048:4096] = tensor(ffn, fm, "W2").ravel()
    for name, begin in (("Qe", 4096), ("Qo", 4352), ("Ke", 4608),
                        ("Ko", 4864), ("Ve", 5120), ("Vo", 5376)):
        result[begin:begin + 256] = tensor(attention, am, name).ravel()
    shared_skip = tensor(attention, am, "skip")
    result[5632:6144] = (tensor(attention, am, "P") * shared_skip[:, None]).ravel()
    result[6144:10240] = tensor(attention, am, "bias").ravel()
    result[10240] = tensor(attention, am, "scale")[0]
    result[10241:10273] = tensor(ffn, fm, "skip")
    result[10273:10305] = shared_skip * shared_skip
    result.tofile(args.output)
    print(f"wrote {result.nbytes} bytes: {args.output}")


if __name__ == "__main__":
    main()
