"""Export original block39 spatial fixture for an independent AMD test."""
from pathlib import Path
import argparse,json,hashlib
import numpy as np
from native_decoder_entry_reference import unpack
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('fixture',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
report=json.loads((a.fixture/'spatial-validation.json').read_text())
assert report['status']=='pass' and report['different']==0 and report['values']==131072
a.output.mkdir(parents=True,exist_ok=False)
for source,dest in [('main-hwc.f32','input.f32'),('skip-hwc.f32','residual.f32')]:
    (a.output/dest).write_bytes((a.fixture/source).read_bytes())
matrix,scale=unpack(a.fixture/'weights.bin')
np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(a.output/'weights.f32')
raw=np.fromfile(a.fixture/'result.output.fp8',np.uint8)
assert not np.any(raw[131072:]) and not np.any((raw[:131072]&127)==127)
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
oracle=e4m3fn(raw[:131072].reshape(-1,8192)[:,inverse]).reshape(4,4,4,4,512).transpose(0,2,1,3,4).reshape(16,16,512)
oracle.tofile(a.output/'oracle.f32')
(a.output/'provenance.json').write_text(json.dumps({'source':str(a.fixture),'oracle_source':'original CUBIN final output, not CPU prediction',
    'raw_oracle_sha256':hashlib.sha256(raw.tobytes()).hexdigest(),'AMD_verified':False},indent=2)+'\n')
