"""Verify actual AMD RGB256-to-head output against independent original chain."""
from pathlib import Path
import numpy as np
import json
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb256')
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
raw=np.fromfile(root/'block30-head.fp8',np.uint8)
assert not np.any(raw[16384:]) and not np.any((raw[:16384]&127)==127)
expected=e4m3fn(raw[:16384]).reshape(2,8192)[:,inverse].reshape(2,4,4,512).transpose(1,2,0,3).reshape(4,4,1024)
actual=np.fromfile(root/'amd/output-rgb256-head.f32','<f4').reshape(expected.shape)
assert np.isfinite(actual).all()
error=np.abs(actual-expected)
print(json.dumps({'RGB_extent':[256,256],'output_extent':[4,4,1024],'different':int(np.count_nonzero(error)),'exact_fraction':float(np.mean(actual==expected)),'mae':float(error.mean()),'max_error':float(error.max()),'scope':'RGB through block30 head, not ViT or game image acceptance'}))
assert np.array_equal(actual,expected),'RGB256 encoder differs from original'
