#!/usr/bin/env python3
"""Convert an FFX RGBA16F capture into DLSS5 block0 8x8 RGB tile input."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("oracle", type=Path)
    parser.add_argument("--source-width", type=int, default=2564)
    parser.add_argument("--source-height", type=int, default=1444)
    parser.add_argument("--render-width", type=int, default=2561)
    parser.add_argument("--render-height", type=int, default=1441)
    parser.add_argument("--target-width", type=int, default=1920)
    parser.add_argument("--target-height", type=int, default=1088)
    args = parser.parse_args()

    row_bytes = args.source_width * 8
    row_pitch = (row_bytes + 255) & ~255
    raw = np.fromfile(args.capture, dtype=np.uint8)
    expected = row_pitch * (args.source_height - 1) + row_bytes
    if raw.size != expected:
        raise ValueError(f"capture size {raw.size} != expected {expected}")
    tight = np.empty((args.source_height, row_bytes), dtype=np.uint8)
    for y in range(args.source_height):
        tight[y] = raw[y * row_pitch : y * row_pitch + row_bytes]
    rgba = tight.copy().view("<f2").reshape(args.source_height, args.source_width, 4)
    rgba = np.nan_to_num(rgba[: args.render_height, : args.render_width].astype(np.float32))
    rgba = np.clip(rgba, 0.0, 1.0)
    image = torch.from_numpy(rgba).permute(2, 0, 1).unsqueeze(0)
    image = F.interpolate(
        image,
        size=(args.target_height, args.target_width),
        mode="bilinear",
        align_corners=False,
    )[0].permute(1, 2, 0).numpy()
    if float(image.max()) <= 1e-6 or float(image.std()) <= 1e-5:
        raise ValueError(
            f"refusing empty/stale FFX Color capture: range={image.min():.7g}..{image.max():.7g} std={image.std():.7g}"
        )
    if args.target_width % 8 or args.target_height % 8:
        raise ValueError("target dimensions must be divisible by 8")
    tiled = (
        image.reshape(args.target_height // 8, 8, args.target_width // 8, 8, 4)
        .transpose(0, 2, 1, 3, 4)
        .copy()
    )
    tiled.astype("<f4").tofile(args.output)
    tile_count = (args.target_width // 8) * (args.target_height // 8)
    with args.oracle.open("wb") as output:
        output.truncate(tile_count * 2048)
    print(
        f"tiles={tile_count} input_bytes={args.output.stat().st_size} "
        f"oracle_bytes={args.oracle.stat().st_size} range={image.min():.7g}..{image.max():.7g}"
    )


if __name__ == "__main__":
    main()
