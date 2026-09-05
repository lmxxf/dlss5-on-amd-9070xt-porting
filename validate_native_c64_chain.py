"""Compare the AMD RGB-to-block5 resident result to original CUBIN output."""
import json
import argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
inverse=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
parser=argparse.ArgumentParser();parser.add_argument('--shift',action='store_true');args=parser.parse_args()
end=7 if args.shift else 5
raw=np.fromfile(f'release/native-c64/block{end}-output.fp8',np.uint8)
assert not np.any(raw[32768:]) and not np.any((raw[:32768]&127)==127)
target=e4m3fn(raw[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(16,32,64)
actual=np.fromfile('release/native-front-chain/output-c64-shift.f32' if args.shift else 'release/native-front-chain/output-c64.f32','<f4').reshape(target.shape)
assert np.isfinite(actual).all()
error=np.abs(actual-target)
print(json.dumps({'shape':[16,32,64],'exact_fraction':float(np.mean(actual==target)),'mae':float(error.mean()),'max_error':float(error.max()),'nonfinite':0,'scope':f'RGB through block0..{end} on AMD; not final RGB or game acceptance'},indent=2))
assert np.array_equal(actual,target), 'resident block0..5 differs from original CUBIN'
