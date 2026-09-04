#!/usr/bin/env python3
"""Extract DirectML matrices and window-attention parameters from portable Swin blobs."""
import argparse, hashlib, json, pathlib
import numpy as np

CONFIG = {
    512: dict(floats=984080, hidden=256, qstride=768, adim=256,
              expand=(0,131072), up=(131072,262144), project=(262144,393216), ffn_skip=(393216,393728),
              qkv=(393728,786944), bias=(786944,852480), scale=(852480,852496), attention_project=(852496,983568), attention_skip=(983568,984080)),
    256: dict(floats=311816, hidden=288, qstride=384, adim=128,
              expand=(0,73728), project=(73728,147456), ffn_skip=(147456,147712),
              qkv=(147712,246016), bias=(246016,278784), scale=(278784,278792), attention_project=(278792,311560), attention_skip=(311560,311816)),
    128: dict(floats=90372, hidden=160, qstride=192, adim=64,
              expand=(0,20480), project=(20480,40960), qkv=(40960,65536), attention_project=(65536,73728),
              bias=(73728,90112), scale=(90112,90116), ffn_skip=(90116,90244), attention_skip=(90244,90372)),
    64: dict(floats=28802, hidden=96, qstride=96, adim=32,
             expand=(0,6144), project=(6144,12288), ffn_skip=(12288,12352), qkv=(12352,18496),
             bias=(18496,26688), scale=(26688,26690), attention_project=(26690,28738), attention_skip=(28738,28802)),
}
p=argparse.ArgumentParser();p.add_argument("input",type=pathlib.Path);p.add_argument("channels",type=int,choices=CONFIG);p.add_argument("output",type=pathlib.Path);a=p.parse_args();c=CONFIG[a.channels];w=np.fromfile(a.input,"<f4")
if w.size!=c["floats"]:raise SystemExit(f"expected {c['floats']} floats, got {w.size}")
a.output.mkdir(parents=True,exist_ok=True);parts={}
for name in ("expand","up","project","ffn_skip","qkv","bias","scale","attention_project","attention_skip"):
    if name not in c:continue
    lo,hi=c[name];value=w[lo:hi]
    if name in ("expand","up"):value=value.reshape(c["hidden"],a.channels).T
    elif name=="project":value=value.reshape(a.channels,c["hidden"]).T
    elif name=="qkv":value=value.reshape(c["qstride"],a.channels).T
    elif name=="attention_project":value=value.reshape(a.channels,c["adim"]).T
    keep=name in ("ffn_skip","attention_skip","bias","scale");path=a.output/f"{a.input.stem}-{name}.{'f32' if keep else 'f16'}";value.astype("<f4" if keep else "<f2").tofile(path);parts[name]={"shape":list(value.shape),"file":path.name,"sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
if "up" not in parts:
    source=a.output/parts["expand"]["file"];path=a.output/f"{a.input.stem}-up.f16";path.write_bytes(source.read_bytes());parts["up"]={"shape":parts["expand"]["shape"],"file":path.name,"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),"unused":True}
manifest={"source":a.input.name,"source_sha256":hashlib.sha256(a.input.read_bytes()).hexdigest(),"channels":a.channels,"hidden":c["hidden"],"qstride":c["qstride"],"attention_dim":c["adim"],"parts":parts}
(a.output/f"{a.input.stem}-directml.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"channels={a.channels} floats={w.size} parts={len(parts)}")
