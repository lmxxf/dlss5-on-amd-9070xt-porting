#!/usr/bin/env python3
"""Prepare blocks31-38 QKV matrices and normalization scales for DirectML."""
import argparse
import hashlib
import json
import pathlib
import numpy as np


def decode_e4m3(v: np.ndarray) -> np.ndarray:
    sign = np.where(v & 0x80, -1.0, 1.0)
    exponent = (v >> 3) & 15
    mantissa = v & 7
    value = sign * (1.0 + mantissa / 8.0) * np.exp2(exponent.astype(np.int16) - 7)
    subnormal = exponent == 0
    value[subnormal] = sign[subnormal] * (mantissa[subnormal] / 8.0) * (2.0 ** -6)
    value[(exponent == 15) & (mantissa == 7)] = np.nan
    return value


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


p = argparse.ArgumentParser()
p.add_argument("source", type=pathlib.Path)
p.add_argument("output", type=pathlib.Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
manifest = {"format": "DirectML FP16 ViT QKV [input, group, output]", "blocks": {}}
for block in range(31, 39):
    source = a.source / ("block31-qkv-effective.fp8" if block == 31 else f"block{block}-qkv-main.fp8")
    raw = np.fromfile(source, dtype=np.uint8)
    if raw.size != 1024 * 3 * 1024:
        raise SystemExit(f"unexpected block{block} QKV bytes: {raw.size}")
    weight = decode_e4m3(raw)
    if not np.all(np.isfinite(weight)):
        raise SystemExit(f"block{block} contains non-finite QKV weight")
    shaped = weight.reshape(1024, 3, 32, 32)
    scales = np.empty((2, 32), dtype=np.float32)
    for group in range(2):
        norms = np.sqrt(np.sum(shaped[:, group] ** 2, axis=-1))
        scales[group] = np.partition(norms, 512, axis=0)[512]
    weight_path = a.output / f"block{block}-qkv-directml.f16"
    scale_path = a.output / f"block{block}-qkv-scales.f32"
    weight.astype("<f2").tofile(weight_path)
    scales.astype("<f4").tofile(scale_path)
    manifest["blocks"][str(block)] = {
        "source": source.name,
        "source_sha256": digest(source),
        "weight": weight_path.name,
        "weight_sha256": digest(weight_path),
        "scale": scale_path.name,
        "scale_sha256": digest(scale_path),
        "scale_min": float(scales.min()),
        "scale_max": float(scales.max()),
    }
(a.output / "directml-vit-weights.json").write_text(
    json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"blocks={len(manifest['blocks'])} output={a.output}")
