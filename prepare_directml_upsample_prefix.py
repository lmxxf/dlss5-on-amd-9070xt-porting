#!/usr/bin/env python3
"""Split a portable decoder 2C->C upsample affine into DirectML matrix+bias."""
import argparse, hashlib, json, pathlib
import numpy as np
p=argparse.ArgumentParser();p.add_argument("input",type=pathlib.Path);p.add_argument("channels",type=int);p.add_argument("matrix_f16",type=pathlib.Path);p.add_argument("bias_f32",type=pathlib.Path);p.add_argument("manifest",type=pathlib.Path);a=p.parse_args();w=np.fromfile(a.input,"<f4");c=a.channels
if w.size!=(2*c+1)*c:raise SystemExit(f"expected {(2*c+1)*c} floats, got {w.size}")
w[:2*c*c].reshape(2*c,c).astype("<f2").tofile(a.matrix_f16);w[2*c*c:].astype("<f4").tofile(a.bias_f32)
d=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();a.manifest.write_text(json.dumps({"source":a.input.name,"source_sha256":d(a.input),"channels":c,"matrix":{"shape":[2*c,c],"file":a.matrix_f16.name,"sha256":d(a.matrix_f16)},"bias":{"shape":[c],"file":a.bias_f32.name,"sha256":d(a.bias_f32)}},indent=2)+"\n")
print(f"channels={c} matrix_bytes={a.matrix_f16.stat().st_size} bias_bytes={a.bias_f32.stat().st_size}")
