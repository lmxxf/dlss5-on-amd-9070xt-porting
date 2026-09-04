#!/usr/bin/env python3
"""Split the distilled block0 MLP into DirectML FP16 matrices and FP32 vectors."""
import argparse, hashlib, json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("input", type=Path)
p.add_argument("output", type=Path)
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
(a.output / "block0-directml.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"source_floats={w.size} parts={len(parts)}")
