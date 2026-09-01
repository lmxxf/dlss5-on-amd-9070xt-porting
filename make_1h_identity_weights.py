#!/usr/bin/env python3
"""Create a diagnostic block1 blob with zero branches and unit skip regions."""

from __future__ import annotations

import argparse
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
    values[4096:4128] = 1  # FFN skip region
    values[-args.tail_half_count:] = 1  # attention tail probe
    values.tofile(args.output)
    print(f"wrote {values.nbytes} bytes: {args.output}")


if __name__ == "__main__":
    main()
