"""Export original512 post70 inputs/oracle and directly decoded coefficients."""
from pathlib import Path
import json
import numpy as np
from native_post70_reference import unpack
base=Path('release/native-post70');source=base/'reference-512-2851';out=base/'amd';out.mkdir(exist_ok=False)
report=json.loads((source/'validation.json').read_text());assert report['status']=='pass' and report['different']==0 and report['values']==786432
for src,dst in [('main-hwc.f32','main.f32'),('skip-hwc.f32','skip.f32'),('color.f32','color.f32')]:(out/dst).write_bytes((source/src).read_bytes())
raw=np.fromfile(source/'output.f32','<f4').reshape(512,512,4);assert np.isfinite(raw).all();raw[:,:,:3].copy().tofile(out/'oracle.f32')
body,sm,ss,head=unpack(base/'smoke/weights.bin');np.concatenate([sm,ss]).astype('<f4').tofile(out/'scales.f32');head.astype('<f4').tofile(out/'head.f32')
w1,w2,q,k,v,p,bias,scale,fs,ats=body
np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(out/'ffn.f32')
np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(out/'attention.f32')
(out/'provenance.json').write_text(json.dumps({'fixture':str(source),'oracle':'original post70 output RGB','mode':'texture-mask1 rgb-mode1','AMD_verified':False},indent=2)+'\n')
