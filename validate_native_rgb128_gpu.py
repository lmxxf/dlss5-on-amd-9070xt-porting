from pathlib import Path
import json,argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb128')
parser=argparse.ArgumentParser();parser.add_argument('--last-block',type=int,choices=[23,29],default=23);args=parser.parse_args()
raw=np.fromfile(root/f'block{args.last_block}-main.fp8',np.uint8);assert not np.any(raw[8192:]) and not np.any((raw[:8192]&127)==127)
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
expected=e4m3fn(raw[:8192][inverse]).reshape(4,4,512)
actual=np.fromfile(root/('amd/output-rgb128-block29.f32' if args.last_block==29 else 'amd/output-rgb128-split.f32'),'<f4').reshape(expected.shape)
assert np.isfinite(actual).all()
err=np.abs(actual-expected)
print(json.dumps({'RGB_extent':[128,128],'last_block':args.last_block,'output_extent':[4,4,512],'exact_fraction':float(np.mean(actual==expected)),'mae':float(err.mean()),'max_error':float(err.max()),'scope':'larger fixture RGB prefix; not live-game acceptance'},indent=2))
assert np.array_equal(actual,expected), 'larger RGB chain differs from original'
