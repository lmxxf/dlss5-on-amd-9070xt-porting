"""Compare the AMD RGB-to-block5 resident result to original CUBIN output."""
import json
import argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
inverse=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
parser=argparse.ArgumentParser();group=parser.add_mutually_exclusive_group();group.add_argument('--shift',action='store_true');group.add_argument('--ds',action='store_true');group.add_argument('--c128',action='store_true');group.add_argument('--c128-shift',action='store_true');group.add_argument('--c128-ds',action='store_true');group.add_argument('--c256',action='store_true');group.add_argument('--c256-shift',action='store_true');group.add_argument('--c256-ds',action='store_true');args=parser.parse_args()
is256=args.c256 or args.c256_shift or args.c256_ds
is128=args.c128 or args.c128_shift or args.c128_ds
end=22 if args.c256_ds else 21 if args.c256_shift else 15 if args.c256 else 14 if args.c128_ds else 13 if args.c128_shift else 9 if args.c128 else 8 if args.ds else 7 if args.shift else 5
raw=np.fromfile(f'release/native-c{256 if is256 else 128 if is128 else 64}/block{end}-'+('aux.fp8' if args.ds or args.c128_ds or args.c256_ds else 'output.fp8'),np.uint8)
count=8192 if args.c128_ds or is256 else 16384 if args.ds or is128 else 32768
assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
if args.c256_ds:
 channels=np.arange(512);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 padded=e4m3fn(raw[:count]).reshape(32,4,4,16).transpose(1,2,0,3).reshape(4,4,512)
 assert not np.any(padded[2:]), 'nonzero C512 physical padding rows'
 target=padded[:2,...,perm]
elif is256:
 inverse256=np.argsort(np.load('release/native-c256/view/mapping.npz')['cell_output_to_hwc'])
 target=e4m3fn(raw[:count].reshape(2,4096)[:,inverse256]).reshape(1,2,4,4,256).transpose(0,2,1,3,4).reshape(4,8,256)
elif args.c128_ds:
 channels=np.arange(256);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 target=e4m3fn(raw[:count]).reshape(16,4,8,16).transpose(1,2,0,3).reshape(4,8,256)[...,perm]
elif is128:
 inverse128=np.argsort(np.load('release/native-c128/view/mapping.npz')['cell_output_to_hwc'])
 target=e4m3fn(raw[:count].reshape(8,2048)[:,inverse128]).reshape(2,4,4,4,128).transpose(0,2,1,3,4).reshape(8,16,128)
elif args.ds:
 channels=np.arange(128);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
 target=e4m3fn(raw[:count]).reshape(8,8,16,16).transpose(1,2,0,3).reshape(8,16,128)[...,perm]
else:target=e4m3fn(raw[:count].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(16,32,64)
actual=np.fromfile('release/native-front-chain/'+('output-c256-ds.f32' if args.c256_ds else 'output-c256-shift.f32' if args.c256_shift else 'output-c256.f32' if args.c256 else 'output-c128-ds.f32' if args.c128_ds else 'output-c128-shift.f32' if args.c128_shift else 'output-c128.f32' if args.c128 else 'output-c64-ds.f32' if args.ds else 'output-c64-shift.f32' if args.shift else 'output-c64.f32'),'<f4').reshape(target.shape)
assert np.isfinite(actual).all()
error=np.abs(actual-target)
print(json.dumps({'shape':list(target.shape),'exact_fraction':float(np.mean(actual==target)),'mae':float(error.mean()),'max_error':float(error.max()),'nonfinite':0,'scope':f'RGB through block0..{end} on AMD; not final RGB or game acceptance'},indent=2))
assert np.array_equal(actual,target), f'resident block0..{end} differs from original CUBIN'
