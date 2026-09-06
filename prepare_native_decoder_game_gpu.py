"""Export original decoder39 output and logical input fixture for AMD."""
from pathlib import Path
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
from native_decoder_entry_reference import unpack
root=Path('release/native-decoder-game');r=json.loads((root/'validation.json').read_text())
assert r['finite'] and r['tail_zero'] and r['checks'][0]['different']==0
inv=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
raw=np.fromfile(root/'result.output.fp8',np.uint8);n=36*60*512
out=e4m3fn(raw[:n].reshape(-1,8192)[:,inv]).reshape(9,15,4,4,512).transpose(0,2,1,3,4).reshape(36,60,512)
out.tofile(root/'oracle.f32')
(root/'input.f32').write_bytes((root/'main.f32').read_bytes());(root/'residual.f32').write_bytes((root/'skip.f32').read_bytes())
matrix,scale=unpack(root/'weights.bin');np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(root/'weights.f32')
