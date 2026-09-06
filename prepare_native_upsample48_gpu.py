"""Export original block48 fixture and decoded coefficients for AMD."""
from pathlib import Path
import argparse,json
import numpy as np
from native_upsample48_reference import unpack
p=argparse.ArgumentParser();p.add_argument('fixture',type=Path);p.add_argument('output',type=Path);p.add_argument('--block56',action='store_true');a=p.parse_args()
report=json.loads((a.fixture/'validation.json').read_text())
assert report['status']=='pass' and report['different']==0
if not a.block56:
    assert report['main_global']=='swap' and not report['skip_control'] and not report['projection_control']
else:assert report['output_extent']==[64,64,128] and report['values']==524288
a.output.mkdir(parents=True,exist_ok=False)
for source,dest in [('input.f32','input.f32'),('skip.f32','residual.f32'),('oracle.f32','oracle.f32')]:
    (a.output/dest).write_bytes((a.fixture/source).read_bytes())
matrix,scale,body=unpack(a.fixture/'weights.bin' if a.block56 else 'release/native-upsample48/block48.weights')
np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(a.output/'weights.f32')
ffn,qkv,projection,bias,scales,skip=body
np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(a.output/'ffn.f32')
np.concatenate([*[m.ravel() for m in qkv],projection.ravel(),bias.ravel(),scales,skip]).astype('<f4').tofile(a.output/'attention.f32')
np.array([report['shift']],'<f4').tofile(a.output/'shift.f32')
(a.output/'provenance.json').write_text(json.dumps({'fixture':str(a.fixture),'oracle':'original block56 output' if a.block56 else 'original block48 output','channels':int(matrix.shape[0]),'AMD_verified':False},indent=2)+'\n')
