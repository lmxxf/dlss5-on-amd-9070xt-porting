#!/usr/bin/env python3
import argparse
from pathlib import Path
import numpy as np

def e4m3(x):
    x=np.asarray(x,np.uint8);s=np.where(x&128,-1.,1.);e=(x>>3)&15;m=x&7
    return (s*np.where(e==0,m/8*2**-6,(1+m/8)*np.exp2(e.astype(np.int16)-7))).astype(np.float32)
def unswizzle(raw,tbits,cbits,half=False):
    x=np.frombuffer(raw,dtype=np.float16).astype(np.float32) if half else e4m3(np.frombuffer(raw,dtype=np.uint8))
    y=np.empty((1<<len(tbits),1<<len(cbits)),np.float32)
    for off,value in enumerate(x):
        token=sum(((off>>bit)&1)<<i for i,bit in enumerate(tbits))
        channel=sum(((off>>bit)&1)<<i for i,bit in enumerate(cbits))
        y[token,channel]=value
    return y
def bit_permutation(columns):
    return np.array([sum(target for bit,target in enumerate(columns) if value>>bit&1) for value in range(1024)])
def main():
    p=argparse.ArgumentParser();p.add_argument("q",type=Path);p.add_argument("k",type=Path);p.add_argument("v",type=Path);p.add_argument("work",type=Path);p.add_argument("attention",type=Path);p.add_argument("output_prefix",type=Path);a=p.parse_args()
    main_forms={"q":([2,6,7,8,14],[0,1,3,4,5,9,10,11,12,13]),"k":([3,6,7,8,14],[0,1,2,4,5,9,10,11,12,13]),"v":([1,0,4,5,2],[3,6,7,8,9,10,11,12,13,14])}
    files={"q":a.q,"k":a.k,"v":a.v};work=a.work.read_bytes();planes={"q":0,"k":2,"v":4};perms={"q":bit_permutation((1,2,8,16,4,32,64,128,256,512)),"k":bit_permutation((1,2,8,16,4,32,64,128,256,512)),"v":bit_permutation((2,1,4,8,16,32,64,128,256,512))};values={}
    for name in "qkv":
        first=unswizzle(files[name].read_bytes()[:32768],*main_forms[name]);begin=planes[name]*65536;second=unswizzle(work[begin:begin+65536],[1,5,6,7,14],[0,2,3,4,8,9,10,11,12,13],True);aligned=np.empty_like(second);aligned[:,perms[name]]=second;values[name]=[first,aligned]
    oracle=unswizzle(a.attention.read_bytes()[:65536],[2,6,7,8,14,15],[1,0,4,5,3,9,10,11,12,13])
    for name in "qkv":np.concatenate([values[name][0],np.zeros_like(values[name][0])],axis=0).astype(np.float32).tofile(str(a.output_prefix)+f"-{name}.f32")
    oracle.astype(np.float32).tofile(str(a.output_prefix)+"-oracle.f32")
if __name__=="__main__":main()
