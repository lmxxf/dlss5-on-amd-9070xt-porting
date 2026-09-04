#!/usr/bin/env python3
"""Split the distilled block0 MLP into DirectML FP16 matrices and FP32 vectors."""
import argparse, hashlib, json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("input", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--permutation", type=Path)
a = p.parse_args()
w = np.fromfile(a.input, "<f4")
if w.size != 645632:
    raise SystemExit(f"expected 645632 float32 values, got {w.size}")
a.output.mkdir(parents=True, exist_ok=True)
parts = {
    "layer1_weight": w[0:49152].reshape(256, 192).T,
    "layer1_bias": w[49152:49408],
    "layer2_weight": w[49408:114944].reshape(256, 256).T,
    "layer2_bias": w[114944:115200],
    "layer3_weight": w[115200:639488].reshape(2048, 256).T,
    "layer3_bias": w[639488:641536],
    "output_bias": w[641536:643584],
    "output_scale": w[643584:645632],
}
manifest = {"source": a.input.name, "source_sha256": hashlib.sha256(a.input.read_bytes()).hexdigest(), "parts": {}}
for name, value in parts.items():
    matrix = name.endswith("weight")
    path = a.output / f"block0-directml-{name}.{'f16' if matrix else 'f32'}"
    value.astype("<f2" if matrix else "<f4").tofile(path)
    manifest["parts"][name] = {"shape": list(value.shape), "file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
if a.permutation:
    permutation = np.fromfile(a.permutation, "<i4")
    if permutation.size != 4096:
        raise SystemExit(f"expected 4096 permutation entries, got {permutation.size}")
    maps = np.empty((4, 512), dtype="<u2")
    source_bits = (4, 0, 1, 3, 2, 5)
    for qy in range(2):
        for qx in range(2):
            entries = []
            for ly in range(4):
                for lx in range(4):
                    x, y = qx * 4 + lx, qy * 4 + ly
                    bits = (x & 1, (x >> 1) & 1, (x >> 2) & 1, y & 1, (y >> 1) & 1, (y >> 2) & 1)
                    token = sum(bits[source_bits[b]] << b for b in range(6))
                    for channel in range(32):
                        entries.append((permutation[token * 64 + channel], (ly * 4 + lx) * 32 + channel))
            entries.sort()
            for rank, (_, logical) in enumerate(entries):
                maps[qy * 2 + qx, logical] = rank
    path = a.output / "block0-directml-tile-map.u16"
    maps.tofile(path)
    manifest["tile_map"] = {"shape": [4, 512], "file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
(a.output / "block0-directml.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"source_floats={w.size} parts={len(parts)}")
