"""Compare the AMD RGB-to-block5 resident result to original CUBIN output."""
import json
import argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
inverse=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
parser=argparse.ArgumentParser();group=parser.add_mutually_exclusive_group();group.add_argument('--shift',action='store_true');group.add_argument('--ds',action='store_true');group.add_argument('--c128',action='store_true');args=parser.parse_args()
end=9 if args.c128 else 8 if args.ds else 7 if args.shift else 5
raw=np.fromfile(f'release/native-c{128 if args.c128 else 64}/block{end}-'+('aux.fp8' if args.ds else 'output.fp8'),np.uint8)
count=16384 if args.ds or args.c128 else 32768
assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
if args.c128:
 inverse128=np.argsort(np.load('release/native-c128/view/mapping.npz')['cell_output_to_hwc'])
 target=e4m3fn(raw[:count].reshape(8,2048)[:,inverse128]).reshape(2,4,4,4,128).transpose(0,2,1,3,4).reshape(8,16,128)
elif args.ds:
 channels=np.arange(128);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 target=e4m3fn(raw[:count]).reshape(8,8,16,16).transpose(1,2,0,3).reshape(8,16,128)[...,perm]
else:target=e4m3fn(raw[:count].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(16,32,64)
actual=np.fromfile('release/native-front-chain/'+('output-c128.f32' if args.c128 else 'output-c64-ds.f32' if args.ds else 'output-c64-shift.f32' if args.shift else 'output-c64.f32'),'<f4').reshape(target.shape)
assert np.isfinite(actual).all()
error=np.abs(actual-target)
print(json.dumps({'shape':list(target.shape),'exact_fraction':float(np.mean(actual==target)),'mae':float(error.mean()),'max_error':float(error.max()),'nonfinite':0,'scope':f'RGB through block0..{end} on AMD; not final RGB or game acceptance'},indent=2))
assert np.array_equal(actual,target), f'resident block0..{end} differs from original CUBIN'
