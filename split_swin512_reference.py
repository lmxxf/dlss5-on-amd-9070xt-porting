#!/usr/bin/env python3
"""Archive-logical FP32 reference for decoder split-Swin blocks 40-47."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


def fast_activation(value: np.ndarray) -> np.ndarray:
    value = np.clip(value, -4.0, 4.0)
    return value * (0.89453125 + value * (0.447265625 - 0.055908203125 * np.abs(value)))


def load_half(path: Path) -> np.ndarray:
    return np.fromfile(path, dtype="<f2").astype(np.float32)


def split_swin(feature: np.ndarray, paths: list[Path]) -> np.ndarray:
    ffwd = load_half(paths[0])
    ffwd_proj = load_half(paths[1])
    qkv_attn = load_half(paths[2])
    projection = load_half(paths[3])
    if [len(ffwd), len(ffwd_proj), len(qkv_attn), len(projection)] != [
        262144, 131584, 458784, 131584
    ]:
        raise ValueError("unexpected split-Swin record sizes")
    gate, up = ffwd.reshape(2, 256, 512)
    hidden = fast_activation(feature @ gate.T) * (feature @ up.T)
    projected = hidden @ ffwd_proj[:131072].reshape(512, 256).T
    feature = projected + feature * ffwd_proj[131072:]

    qkv = feature @ qkv_attn[:393216].reshape(3, 256, 512).transpose(0, 2, 1)
    q, k, v = (item.reshape(64, 16, 16).transpose(1, 0, 2) for item in qkv)
    q /= np.maximum(np.linalg.norm(q, axis=-1, keepdims=True), 1e-12)
    k /= np.maximum(np.linalg.norm(k, axis=-1, keepdims=True), 1e-12)
    bias = qkv_attn[393216:458752].reshape(16, 64, 64)
    scale = np.frombuffer(
        qkv_attn[458752:458784].astype("<f2").tobytes(), dtype="<f4", count=16
    ).reshape(16, 1, 1)
    logits = np.einsum("hqd,hkd->hqk", q, k) * scale + bias
    logits -= logits.max(axis=-1, keepdims=True)
    attention = np.exp(logits)
    attention /= attention.sum(axis=-1, keepdims=True)
    attended = np.einsum("hqk,hkd->qhd", attention, v).reshape(64, 256)
    output = attended @ projection[:131072].reshape(512, 256).T
    return output + feature * projection[131072:]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("weights", type=Path, nargs=4)
    args = parser.parse_args()
    feature = np.fromfile(args.input, dtype="<f4").reshape(64, 512)
    result = split_swin(feature, args.weights)
    result.astype("<f4").tofile(args.output)
    print(
        f"finite={np.isfinite(result).all()} range={result.min():.9g}..{result.max():.9g} "
        f"mean={result.mean():.9g} std={result.std():.9g}"
    )


if __name__ == "__main__":
    main()
