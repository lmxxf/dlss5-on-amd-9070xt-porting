#!/usr/bin/env python3
"""Convert block70 CSR prefix weights into two fixed contributions per output."""
import argparse, hashlib, json, struct
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument("input", type=Path)
p.add_argument("output", type=Path)
p.add_argument("manifest", type=Path)
a = p.parse_args()
raw = a.input.read_bytes()
ends = np.frombuffer(raw, "<u4", count=2049)
nnz = struct.unpack_from("<I", raw, 8192)[0]
indices = np.frombuffer(raw, "<u4", count=nnz, offset=8200)
weight_offset = 8200 + nnz * 4
stored_weights = (len(raw) - weight_offset) // 4
weights = np.zeros(nnz, dtype="<f4")
weights[:stored_weights] = np.frombuffer(raw, "<f4", count=stored_weights, offset=weight_offset)
degree = np.diff(ends)
if ends[0] != 0 or ends[-1] != nnz or degree.min() != 1 or degree.max() != 2:
    raise SystemExit("expected every output to have one or two contributions")
records = np.zeros(2048, dtype=[("i0", "<u4"), ("i1", "<u4"), ("w0", "<f4"), ("w1", "<f4")])
for o in range(2048):
    lo, hi = int(ends[o]), int(ends[o + 1])
    records[o]["i0"], records[o]["w0"] = indices[lo], weights[lo]
    if hi - lo == 2:
        records[o]["i1"], records[o]["w1"] = indices[lo + 1], weights[lo + 1]
records.tofile(a.output)
manifest = {
    "source": a.input.name,
    "source_sha256": hashlib.sha256(raw).hexdigest(),
    "output": a.output.name,
    "output_sha256": hashlib.sha256(a.output.read_bytes()).hexdigest(),
    "record": ["uint input0", "uint input1", "float weight0", "float weight1"],
    "records": 2048,
    "degree_1": int((degree == 1).sum()),
    "degree_2": int((degree == 2).sum()),
    "stored_weights": stored_weights,
    "implicit_zero_tail_weights": int(nnz - stored_weights),
}
a.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
print(manifest)
