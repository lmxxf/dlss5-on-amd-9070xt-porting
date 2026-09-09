#!/usr/bin/env python3
"""Infer the byte-level record split used by DLSSNR's 1024-wide ViT blocks.

This deliberately stops at byte regions.  The large matrices are consumed as
packed E4M3 by the original CUBIN and still need a tensor-core unswizzle before
they can become canonical row-major tensors.
"""

import argparse
import json
import math
import struct
from pathlib import Path


MATRIX = 1024 * 1024


def section(name, offset, size, storage, shape=None, evidence=None):
    value = {
        "name": name,
        "byte_offset": offset,
        "byte_size": size,
        "storage": storage,
    }
    if shape is not None:
        value["logical_shape"] = shape
    if evidence is not None:
        value["evidence"] = evidence
    return value


def expected(layer):
    if layer == 0:
        return [
            section("ffn_expand_weight", 0, 4 * MATRIX, "packed_e4m3",
                    [4096, 1024]),
            section("alignment_padding", 4 * MATRIX, 16, "zero_padding"),
        ]
    if layer == 1:
        return [
            section("ffn_contract_weight", 0, 4 * MATRIX, "packed_e4m3",
                    [1024, 4096]),
            section("ffn_cos_skip", 4 * MATRIX, 2048, "float16", [1024],
                    "CUBIN loads residual coefficients at weight+0x400000"),
        ]
    if layer == 2:
        return [
            section("qkv_weight", 0, 3 * MATRIX, "packed_e4m3",
                    [3072, 1024]),
            section("attention_scale_region", 3 * MATRIX, 128,
                    "unresolved_mixed_region", evidence=(
                        "weight-names calls this attn_scale; exact scalar "
                        "packing still requires SASS address recovery")),
        ]
    if layer == 3:
        return [section("attention_record", 0, 2, "float16", [1])]
    if layer == 4:
        return [
            section("projection_weight", 0, MATRIX, "packed_e4m3",
                    [1024, 1024]),
            section("attn_cos_skip", MATRIX, 2048, "float16", [1024],
                    "CUBIN loads residual coefficients at weight+0x100000"),
        ]
    raise ValueError(layer)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("index", type=Path, help="weights-arena-index.json")
    parser.add_argument("--arena", type=Path,
                        help="optional flat arena for content validation")
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    records = json.loads(args.index.read_text())
    if isinstance(records, dict):
        records = records["records"]
    by_name = {record["name"]: record for record in records}
    arena = args.arena.read_bytes() if args.arena else None
    blocks = {}
    for block in range(31, 39):
        layers = []
        for layer in range(5):
            name = f"block{block}.layer{layer}.layer"
            record = by_name[name]
            sections = expected(layer)
            total = sum(item["byte_size"] for item in sections)
            if total != record["payload_size"]:
                raise SystemExit(
                    f"{name}: inferred {total} bytes != record "
                    f"{record['payload_size']} bytes")
            entry = {
                "record": name,
                "arena_offset": record["arena_offset"],
                "payload_size": record["payload_size"],
                "sections": sections,
            }
            if arena is not None:
                begin = record["arena_offset"]
                payload = arena[begin:begin + record["payload_size"]]
                if len(payload) != record["payload_size"]:
                    raise SystemExit(f"{name}: arena is truncated")
                checks = {}
                if layer == 0:
                    checks["padding_all_zero"] = not any(payload[-16:])
                    if not checks["padding_all_zero"]:
                        raise SystemExit(f"{name}: nonzero Expand padding")
                if layer in (1, 4):
                    values = struct.unpack("<1024e", payload[-2048:])
                    if not all(math.isfinite(value) for value in values):
                        raise SystemExit(f"{name}: non-finite residual skip")
                    checks["residual_skip_min"] = min(values)
                    checks["residual_skip_max"] = max(values)
                entry["content_checks"] = checks
            layers.append(entry)
        blocks[str(block)] = layers

    result = {
        "status": "byte regions closed; packed matrices still require unswizzle",
        "width": 1024,
        "tokens_at_256x144": 64,
        "content_validated": arena is not None,
        "blocks": blocks,
    }
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
