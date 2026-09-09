"""256-token original expand layout and arithmetic validation."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
from native_vit_linear_reference import expand,unpack_expand
root=Path('release/native-vit/expand-256-3003');n=256;channels=4096
x=np.load('release/native-vit/attention-random-256-3003/logical.npz')['q']
raw=np.fromfile(root/'expand-1.fp8',np.uint8)
actual=np.empty((n,channels),np.float32)
actual[bits(n*channels,[2,6,7,8,16,17,18,19]),bits(n*channels,[0,1,3,4,5,9,10,11,12,13,14,15])]=e4m3fn(raw[:n*channels])
expected=expand(x,unpack_expand('release/native-vit/block31-expand.weights'))
report={'tokens':n,'values':actual.size,'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'tail_zero':not bool(raw[n*channels:].any()),'replay_identical':(root/'expand-1.fp8').read_bytes()==(root/'expand-2.fp8').read_bytes(),'scope':'original expand only; not AMD or game acceptance'}
print(json.dumps(report,indent=2));(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['different']==0 and report['finite'] and report['tail_zero'] and report['replay_identical']
x.astype('<f4').tofile(root/'input.f32')
actual.astype('<f4').tofile(root/'oracle.f32')
unpack_expand('release/native-vit/block31-expand.weights').astype('<f4').tofile(root/'weights.f32')
