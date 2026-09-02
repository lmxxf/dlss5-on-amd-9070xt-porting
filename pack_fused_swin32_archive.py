#!/usr/bin/env python3
"""Pack archive-logical 32-channel fused Swin into block1 AMD layout."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
def main():
 p=argparse.ArgumentParser();p.add_argument('weights',type=Path);p.add_argument('output',type=Path);a=p.parse_args();v=np.fromfile(a.weights,'<f2').astype('f4')
 if len(v)==11392:w1,w2,fs,qkv,bias,scale,proj,ats=v[1024:3072],v[3072:5120],v[5120:5152],v[5200:6736],v[6736:10832],v[10832:10840],v[10840:11352],v[11352:11384]
 elif len(v)==10336:w1,w2,fs,qkv,bias,scale,proj,ats=v[:2048],v[2048:4096],v[4112:4144],v[4144:5680],v[5680:9776],v[9776:9784],v[9784:10296],v[10296:10328]
 else:raise ValueError(len(v))
 q=qkv.reshape(3,16,32);out=np.empty(10305,'f4');out[:2048]=w1;out[2048:4096]=w2
 cursor=4096
 for g in range(3):
  out[cursor:cursor+256]=q[g,:,0::2].ravel();cursor+=256;out[cursor:cursor+256]=q[g,:,1::2].ravel();cursor+=256
 out[5632:6144]=proj;out[6144:10240]=bias;out[10240]=np.frombuffer(scale.astype('<f2').tobytes(),'<f4',count=1)[0];out[10241:10273]=fs;out[10273:10305]=ats;out.tofile(a.output);print(a.output,out.nbytes)
if __name__=='__main__':main()
