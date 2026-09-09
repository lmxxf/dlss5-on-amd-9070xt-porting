"""Pack logical inputs and independent RTX output for the AMD attention test."""
import argparse
import numpy as np
from pathlib import Path
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args()
d=np.load(a.folder/'logical.npz');n=d['q'].shape[0]
raw=np.fromfile(a.folder/'rtx-output-1.fp8',np.uint8)
assert (a.folder/'rtx-output-1.fp8').read_bytes()==(a.folder/'rtx-output-2.fp8').read_bytes()
assert not raw[n*1024:].any()
t=bits(n*1024,[2,6,7,8,14,15]+list(range(16,10+(n-1).bit_length())))
c=bits(n*1024,[0,1,3,4,5,9,10,11,12,13])
oracle=np.empty((n,1024),np.float32);oracle[t,c]=e4m3fn(raw[:n*1024])
values=np.concatenate([d[x] for x in ('q','k','v')]).astype(np.float32)
assert np.isfinite(values).all() and np.isfinite(oracle).all()
values.tofile(a.folder/'input.f32');oracle.tofile(a.folder/'oracle.f32')
