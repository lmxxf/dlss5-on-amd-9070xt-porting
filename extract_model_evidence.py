#!/usr/bin/env python3
"""Extract model-structure evidence from the leaked DLSSNR DLL.

This script intentionally separates three evidence layers:

1. compiled kernel families (the available toolbox),
2. diagnostic strings that expose input relationships,
3. numbered records found inside the embedded weight archive.

It does not claim that kernel presence equals execution order.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
from collections import defaultdict
from pathlib import Path


KERNEL_PATTERNS = (
    re.compile(rb"cc_tinlayout_fused_swin_(1|2|4|8)h_(32|64|128|256)_([1248])(?:_|\x00)"),
    re.compile(rb"cc_split_swin_16h_[a-z_]*512(?:_|\x00)"),
    re.compile(rb"cc_dec_input_upsample_1024_512(?:_|\x00)"),
)

DIRECT_EVIDENCE = (
    b"CCVitAttentionLayer specialized for Cin=1024",
    b"CCDecInputUpsampleLayer currently specialized for 1024->512 (dec5)",
    b"CCDecInputUpsampleLayer requires 2 inputs (main, skip)",
    b"CCTinlayoutFusedPostBlockSwin1HLayer requires 2 inputs (main + enc0 skip)",
    b"CCTinlayoutFusedSwin1HLayer (upsample) requires 2 inputs (main + skip)",
    b"CCTinlayoutFusedSwin2HLayer (upsample) requires 2 inputs (main + skip)",
    b"CCTinlayoutFusedSwin4HLayer (upsample) requires 2 inputs (main + skip)",
    b"CCTinlayoutFusedSwin8HLayer (upsample) requires 2 inputs (main + skip)",
    b"layer0.conv_weight",
    b"out_conv_weight",
)

WEIGHT_RECORD = re.compile(rb"block([0-9]+)\.layer([0-9]+)\.layer")


def printable(value: bytes) -> str:
    return value.decode("ascii", "replace")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    args = parser.parse_args()
    data = args.dll.read_bytes()

    print(f"file: {args.dll}")
    print(f"size: {len(data)} bytes")
    print(f"sha256: {hashlib.sha256(data).hexdigest()}")

    print("\nKernel-family evidence (unique matches):")
    for pattern in KERNEL_PATTERNS:
        matches = sorted({match.group(0).rstrip(b"_\0") for match in pattern.finditer(data)})
        for match in matches:
            print(f"  {printable(match)}")

    print("\nDirect topology/width evidence:")
    for needle in DIRECT_EVIDENCE:
        offsets = [match.start() for match in re.finditer(re.escape(needle), data)]
        state = ", ".join(f"0x{offset:x}" for offset in offsets) if offsets else "NOT FOUND"
        print(f"  {printable(needle)} @ {state}")

    records: dict[int, set[int]] = defaultdict(set)
    offsets: dict[tuple[int, int], int] = {}
    for match in WEIGHT_RECORD.finditer(data):
        block = int(match.group(1))
        layer = int(match.group(2))
        records[block].add(layer)
        offsets.setdefault((block, layer), match.start())

    print("\nNumbered weight records:")
    if not records:
        print("  none")
        return
    block_ids = sorted(records)
    first_record = min(match.start() for match in WEIGHT_RECORD.finditer(data))
    archive_start = first_record - 16
    declared_size = struct.unpack_from("<Q", data, archive_start)[0]
    first_name_length = struct.unpack_from("<Q", data, archive_start + 8)[0]
    print(f"  inferred archive start: 0x{archive_start:x}")
    print(f"  declared archive size: {declared_size} bytes")
    print(f"  first name length: {first_name_length} bytes")
    contiguous = block_ids == list(range(block_ids[0], block_ids[-1] + 1))
    print(f"  block range: {block_ids[0]}..{block_ids[-1]}")
    print(f"  distinct blocks: {len(block_ids)}")
    print(f"  contiguous: {contiguous}")
    print(f"  distinct block/layer prefixes: {sum(len(layers) for layers in records.values())}")
    print("  layers per block:")
    for block in block_ids:
        layer_list = ",".join(str(layer) for layer in sorted(records[block]))
        first_offset = min(offsets[(block, layer)] for layer in records[block])
        print(f"    block{block}: layers {layer_list} (first @ 0x{first_offset:x})")

    print("\nBoundary:")
    print("  Kernel presence proves compiled availability, not runtime execution order.")
    print("  Numbered weight records prove serialization groups, not architectural layers.")
    print("  A block-to-kernel map still requires the network descriptor or CPU build logic.")


if __name__ == "__main__":
    main()
