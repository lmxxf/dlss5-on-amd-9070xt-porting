#!/usr/bin/env python3
"""Extract row-major DirectML matrices from a portable 512-channel Swin blob."""
import argparse
import hashlib
import json
import pathlib
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("input", type=pathlib.Path)
p.add_argument("output", type=pathlib.Path)
a = p.parse_args()
w = np.fromfile(a.input, dtype="<f4")
if w.size != 984080:
    raise SystemExit(f"unexpected 512-channel blob floats: {w.size}")
a.output.mkdir(parents=True, exist_ok=True)
parts = {
    "gate": w[0:131072].reshape(256, 512).T,
    "up": w[131072:262144].reshape(256, 512).T,
    "project": w[262144:393216].reshape(512, 256).T,
    "ffn_skip": w[393216:393728],
    "qkv": w[393728:786944].reshape(768, 512).T,
    "attention_project": w[852496:983568].reshape(512, 256).T,
    "attention_skip": w[983568:984080],
}
manifest = {"source": a.input.name, "source_sha256": hashlib.sha256(a.input.read_bytes()).hexdigest(), "parts": {}}
for name, value in parts.items():
    path = a.output / f"{a.input.stem}-{name}.{'f32' if 'skip' in name else 'f16'}"
    value.astype("<f4" if "skip" in name else "<f2").tofile(path)
    manifest["parts"][name] = {"shape": list(value.shape), "file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
(a.output / f"{a.input.stem}-directml.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"source_floats={w.size} parts={len(parts)}")
