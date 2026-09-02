#!/usr/bin/env python3
"""Pack archive-logical 64-channel fused Swin tensors for AMD runner."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main():
 p=argparse.ArgumentParser();p.add_argument('weights',type=Path);p.add_argument('output',type=Path);a=p.parse_args();v=np.fromfile(a.weights,'<f2').astype('f4')
 if len(v)==35024:w1,w2,fs,qkv,bias,scale,proj,ats=v[6144:12288],v[12288:18432],v[18432:18496],v[18560:24704],v[24704:32896],v[32896:32900],v[32900:34948],v[34948:35012]
 elif len(v)==30880:w1,w2,fs,qkv,bias,scale,proj,ats=v[2048:8192],v[8192:14336],v[14352:14416],v[14416:20560],v[20560:28752],v[28752:28756],v[28756:30804],v[30804:30868]
 else:raise ValueError(len(v))
 out=np.empty(28802,'f4');out[:6144]=w1;out[6144:12288]=w2;out[12288:12352]=fs;out[12352:18496]=qkv;out[18496:26688]=bias;out[26688:26690]=np.frombuffer(scale.astype('<f2').tobytes(),'<f4',count=2);out[26690:28738]=proj;out[28738:28802]=ats;out.tofile(a.output);print(a.output,out.nbytes)
if __name__=='__main__':main()
