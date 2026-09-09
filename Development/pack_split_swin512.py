#!/usr/bin/env python3
"""Pack four archive split-Swin records into the AMD 512-channel layout."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("weights", type=Path, nargs=4)
    args = parser.parse_args()
    records = [np.fromfile(path, dtype="<f2").astype(np.float32)
               for path in args.weights]
    if [len(item) for item in records] != [262144, 131584, 458784, 131584]:
        raise ValueError("unexpected split-Swin record sizes")
    output = np.empty(984080, dtype="<f4")
    output[:262144] = records[0]
    output[262144:393216] = records[1][:131072]
    output[393216:393728] = records[1][131072:]
    output[393728:786944] = records[2][:393216]
    output[786944:852480] = records[2][393216:458752]
    output[852480:852496] = np.frombuffer(
        records[2][458752:].astype("<f2").tobytes(), dtype="<f4", count=16)
    output[852496:983568] = records[3][:131072]
    output[983568:984080] = records[3][131072:]
    output.tofile(args.output)
    print(f"wrote {output.nbytes} bytes: {args.output}")


if __name__ == "__main__":
    main()
