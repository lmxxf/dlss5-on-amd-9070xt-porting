#!/usr/bin/env python3
"""Create a diagnostic block1 blob with zero branches and unit skip regions."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--tail-half-count", type=int, default=32,
        help="number of trailing FP16 slots forced to one while probing attn skip packing",
    )
    args = parser.parse_args()
    values = np.zeros(10336, dtype="<f2")
    values[4104:4136] = 1  # FFN skip region
    values[10296 : 10296 + args.tail_half_count] = 1  # attention skip probe
    raw = values.view(np.uint8)
    raw[9768 * 2 : 9768 * 2 + 4] = np.frombuffer(struct.pack("<f", 1.0), dtype=np.uint8)
    values.tofile(args.output)
    print(f"wrote {values.nbytes} bytes: {args.output}")


if __name__ == "__main__":
    main()
