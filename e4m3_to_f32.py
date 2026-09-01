#!/usr/bin/env python3
import argparse, math, pathlib, struct
p=argparse.ArgumentParser();p.add_argument("input",type=pathlib.Path);p.add_argument("output",type=pathlib.Path);p.add_argument("--count",type=int,required=True);a=p.parse_args()
d=a.input.read_bytes()[:a.count];out=bytearray()
for v in d:
    sign=-1.0 if v&0x80 else 1.0;e=(v>>3)&0xf;m=v&7
    if e==0:x=sign*(m/8.0)*(2.0**-6)
    elif e==15 and m==7:x=math.nan
    else:x=sign*(1.0+m/8.0)*(2.0**(e-7))
    out+=struct.pack('<f',x)
a.output.write_bytes(out);print(f"input={len(d)} output={len(out)}")
