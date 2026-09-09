#!/usr/bin/env python3
"""Pack archive-logical 128-channel fused Swin tensors for AMD runner."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main():
 p=argparse.ArgumentParser();p.add_argument('weights',type=Path);p.add_argument('output',type=Path);a=p.parse_args();v=np.fromfile(a.weights,'<f2').astype('f4')
 if len(v)==115088:w1,w2,fs,qkv,bias,scale,proj,ats=v[24576:45056],v[45056:65536],v[65536:65664],v[65792:90368],v[90368:106752],v[106752:106760],v[106760:114952],v[114952:115080]
 elif len(v)==98592:w1,w2,fs,qkv,bias,scale,proj,ats=v[8192:28672],v[28672:49152],v[49168:49296],v[49296:73872],v[73872:90256],v[90256:90264],v[90264:98456],v[98456:98584]
 else:raise ValueError(len(v))
 out=np.empty(90372,'f4');out[:20480]=w1;out[20480:40960]=w2;out[40960:65536]=qkv;out[65536:73728]=proj;out[73728:90112]=bias;out[90112:90116]=np.frombuffer(scale.astype('<f2').tobytes(),'<f4',count=4);out[90116:90244]=fs;out[90244:90372]=ats;out.tofile(a.output);print(a.output,out.nbytes)
if __name__=='__main__':main()
