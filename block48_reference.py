#!/usr/bin/env python3
"""Archive-logical block48 reference with canonical main/skip inputs."""

from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
import torch
from block0_reference import swin_block

def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("weights", type=Path)
    p.add_argument("main", type=Path, help="16x16x512 FP32")
    p.add_argument("output", type=Path)
    p.add_argument("--skip", type=Path, help="optional canonical 18x32x256 FP32")
    a = p.parse_args()
    v = np.fromfile(a.weights, dtype="<f2").astype(np.float32)
    x = np.fromfile(a.main, dtype="<f4").reshape(16, 16, 512)
    prefix = v[:65536].reshape(256, 256)
    x = np.concatenate((x[..., :256] @ prefix[:128].T,
                        x[..., 256:] @ prefix[128:].T), axis=-1)
    x = np.repeat(np.repeat(x, 2, axis=0), 2, axis=1)[:18]
    if a.skip:
        x += np.fromfile(a.skip, dtype="<f4").reshape(18, 32, 256)
    scale = np.frombuffer(v[377344:377360].astype("<f2").tobytes(),
                          dtype="<f4", count=8).copy()
    w = {
        "weight1": torch.from_numpy(v[98304:172032].reshape(288, 256)),
        "weight2": torch.from_numpy(v[172032:245760].reshape(256, 288)),
        "ffn_cos_skip": torch.from_numpy(v[245760:246016]),
        "qkv_weight": torch.from_numpy(v[246272:344576].reshape(384, 256)),
        "attn_bias": torch.from_numpy(v[344576:377344].reshape(8, 64, 64)),
        "attn_scale": torch.from_numpy(scale),
        "projection_weight": torch.from_numpy(v[377360:410128].reshape(256, 128)),
        "attn_cos_skip": torch.from_numpy(v[410128:410384]),
    }
    with torch.inference_mode():
        y = swin_block(torch.from_numpy(x).unsqueeze(0), w, shifted=False)[0].numpy()
    y.astype("<f4").tofile(a.output)
    print(f"finite={np.isfinite(y).all()} shape={y.shape} range={y.min():.9g}..{y.max():.9g} std={y.std():.9g}")

if __name__ == "__main__": main()
