#!/usr/bin/env python3
"""Infer and validate the repeated fused-Swin flat tensor layouts."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def layout(width: int) -> list[tuple[str, int]]:
    heads = width // 32
    hidden = 64 if width == 32 else width + 32
    tensors: list[tuple[str, int]] = []
    if width != 32:
        tensors.append(("weight0", width * (width // 2)))
    tensors.extend(
        [
            ("weight1", hidden * width),
            ("weight2", width * hidden),
            ("pre_ffn_padding", 8),
            ("ffn_cos_skip", width),
            ("qkv_weight", 3 * (width // 2) * width),
            ("attn_bias", heads * 64 * 64),
            ("attn_scale_float32_storage", 16),
            ("projection_weight", width * (width // 2)),
            ("attn_cos_skip", width),
            ("tail_padding", 16 if width == 256 else 8),
        ]
    )
    return tensors


def main() -> None:
    records = json.loads((ROOT / "weights-index.json").read_text(encoding="utf-8"))
    by_name = {record["name"]: record for record in records}
    stages = {
        32: [1, 2, 3, 67, 68, 69],
        64: [5, 6, 7, 63, 64, 65],
        128: [9, 10, 11, 12, 13, 57, 58, 59, 60, 61],
        256: [15, 16, 17, 18, 19, 20, 21, 49, 50, 51, 52, 53, 54, 55],
    }
    output: dict[str, object] = {"stages": {}}
    for width, blocks in stages.items():
        tensors = layout(width)
        inferred = sum(size for _, size in tensors)
        stage = {"width": width, "inferred_elements": inferred, "tensors": []}
        offset = 0
        for name, size in tensors:
            stage["tensors"].append(
                {"name": name, "element_offset": offset, "element_count": size}
            )
            offset += size
        checks = []
        for block in blocks:
            actual = int(by_name[f"block{block}.layer0.layer"]["element_count"])
            padding = actual - inferred
            if padding != 0:
                raise ValueError(
                    f"block{block} width {width}: actual={actual}, inferred={inferred}"
                )
            checks.append({"block": block, "actual_elements": actual, "tail_padding": padding})
        stage["blocks"] = checks
        output["stages"][str(width)] = stage
        print(f"width={width}: inferred={inferred}, blocks={len(blocks)}, closed=yes")
    (ROOT / "fused-layouts.json").write_text(
        json.dumps(output, indent=2) + "\n", encoding="utf-8"
    )
    print(f"wrote: {ROOT / 'fused-layouts.json'}")


if __name__ == "__main__":
    main()
