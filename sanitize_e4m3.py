#!/usr/bin/env python3
import argparse, pathlib
p=argparse.ArgumentParser();p.add_argument("input",type=pathlib.Path);p.add_argument("output",type=pathlib.Path);a=p.parse_args()
d=bytearray(a.input.read_bytes());count=0
for i,v in enumerate(d):
    if v==0x7f:d[i]=0x7e;count+=1
    elif v==0xff:d[i]=0xfe;count+=1
a.output.write_bytes(d);print(f"replaced={count} bytes={len(d)}")
