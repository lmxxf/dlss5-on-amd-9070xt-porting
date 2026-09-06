"""Export original C32 upsample oracle, half-merge coefficients and C32 body."""
from pathlib import Path
import json
import numpy as np
from native_upsample66_reference import unpack
base=Path('release/native-upsample66');source=base/'spatial-256-2707-3';out=base/'amd';out.mkdir(exist_ok=False)
report=json.loads((source/'validation.json').read_text());assert report['status']=='pass' and report['different']==0 and report['values']==2097152
for src,dst in [('input.f32','input.f32'),('skip.f32','residual.f32'),('oracle.f32','oracle.f32')]:(out/dst).write_bytes((source/src).read_bytes())
matrix,scale,body=unpack(base/'weights.bin');np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(out/'weights.f32')
w1,w2,q,k,v,p,bias,scale,fs,ats=body
np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(out/'ffn.f32')
np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(out/'attention.f32')
np.array([report['shift']],'<f4').tofile(out/'shift.f32')
(out/'provenance.json').write_text(json.dumps({'fixture':str(source),'channels':32,'oracle':'original block66 output','AMD_verified':False},indent=2)+'\n')
