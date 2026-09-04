#!/usr/bin/env python3
"""Convert the portable block39 affine+bias matrix for DirectML."""
import argparse
import hashlib
import json
import pathlib
import numpy as np

p=argparse.ArgumentParser();p.add_argument("input",type=pathlib.Path);p.add_argument("matrix_f16",type=pathlib.Path);p.add_argument("bias_f32",type=pathlib.Path);p.add_argument("manifest",type=pathlib.Path);a=p.parse_args()
w=np.fromfile(a.input,dtype="<f4")
if w.size!=(1536+1)*512:raise SystemExit(f"unexpected floats: {w.size}")
w[:1536*512].astype("<f2").tofile(a.matrix_f16);w[1536*512:].astype("<f4").tofile(a.bias_f32)
digest=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
a.manifest.write_text(json.dumps({"source":a.input.name,"source_sha256":digest(a.input),"matrix":{"shape":[1536,512],"file":a.matrix_f16.name,"sha256":digest(a.matrix_f16)},"bias":{"shape":[512],"file":a.bias_f32.name,"sha256":digest(a.bias_f32)}},indent=2)+"\n")
print(f"matrix_bytes={a.matrix_f16.stat().st_size} bias_bytes={a.bias_f32.stat().st_size}")
