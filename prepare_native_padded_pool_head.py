"""Independent original pool->head output for padded AMD pool/head testing."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512/pool-game-real')
report=json.loads((root/'validation.json').read_text())
assert report['poison_output'] and report['main_different']==report['valid_different']==report['bottom_nonzero']==report['right_nonzero']==0
weights=Path('release/native-c512/block30-4.weights')
subprocess.run(['/tmp/native-split-head-oracle',str(root/'pool.fp8'),str(root/'head.fp8'),str(weights),'32','20','16','3','1','8'],check=True)
raw=np.fromfile(root/'head.fp8',np.uint8);n=640*1024
assert not raw[n:].any() and not np.any((raw[:n]&127)==127)
inv=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
oracle=e4m3fn(raw[:n].reshape(-1,2,8192)[:,:,inv]).reshape(-1,2,4,4,512).transpose(0,2,3,1,4).reshape(5,8,4,4,1024).transpose(0,2,1,3,4).reshape(20,32,1024)
w=np.fromfile(weights,np.uint8);matrix=np.empty((1024,512),np.float32)
matrix[bits(524288,[3,6,7,8,9,10,11,12,13,14]),bits(524288,[1,0,4,5,2,15,16,17,18])]=e4m3fn(w[:524288])
pool=np.fromfile(root/'pool-oracle.f32',np.float32).reshape(20,32,512)
expected=F(multiply(pool,matrix));assert np.isfinite(oracle).all() and np.array_equal(expected,oracle)
oracle.tofile(root/'head-oracle.f32');matrix.tofile(root/'head-weights.f32')
result={'scope':'original real-weight pool->head; AMD input raw-projection remains a CPU reference fixture','values':n,'different':0,'padded_bottom_nonzero':int(np.count_nonzero(oracle[18:])),'padded_right_nonzero':int(np.count_nonzero(oracle[:18,30:]))}
(root/'head-validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
