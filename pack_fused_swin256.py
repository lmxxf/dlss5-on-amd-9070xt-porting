#!/usr/bin/env python3
"""Pack archive-logical 256-channel fused Swin tensors for AMD runner."""

from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np

def main() -> None:
    p=argparse.ArgumentParser();p.add_argument("weights",type=Path);p.add_argument("output",type=Path);a=p.parse_args()
    v=np.fromfile(a.weights,dtype="<f2").astype(np.float32)
    if len(v)==410392:
        w1,w2,skip,qkv,bias,scale,proj,ats=(v[98304:172032],v[172032:245760],v[245760:246016],v[246272:344576],v[344576:377344],v[377344:377360],v[377360:410128],v[410128:410384])
    elif len(v)==410144:
        # Encoder block22: regular 256-channel body followed by a 256x256
        # downsample projection (the usual 8-half tail is replaced by it).
        w1,w2,skip,qkv,bias,scale,proj,ats=(v[32768:106496],v[106496:180224],v[180240:180496],v[180496:278800],v[278800:311568],v[311568:311584],v[311584:344352],v[344352:344608])
    elif len(v)==344616:
        w1,w2,skip,qkv,bias,scale,proj,ats=(v[32768:106496],v[106496:180224],v[180240:180496],v[180496:278800],v[278800:311568],v[311568:311584],v[311584:344352],v[344352:344608])
    else: raise ValueError(f"unexpected record size {len(v)}")
    out=np.empty(311816,np.float32);out[:73728]=w1;out[73728:147456]=w2;out[147456:147712]=skip;out[147712:246016]=qkv;out[246016:278784]=bias;out[278784:278792]=np.frombuffer(scale.astype('<f2').tobytes(),'<f4',count=8);out[278792:311560]=proj;out[311560:311816]=ats;out.tofile(a.output);print(f"wrote {out.nbytes} bytes: {a.output}")
if __name__=="__main__":main()
