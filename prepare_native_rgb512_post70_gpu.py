"""Export final post coefficients and the same initial RGB for resident use."""
from pathlib import Path
import json
import numpy as np
from native_post70_reference import unpack
root=Path('release/native-rgb512');out=root/'amd-final70';out.mkdir(exist_ok=False)
report=json.loads((root/'post70/validation.json').read_text());assert report['status']=='pass' and report['different']==0
color=(root/'post70/color.f32').read_bytes();assert color==(root/'input-hwc.rgba32f').read_bytes();(out/'post70-color.f32').write_bytes(color)
body,sm,ss,head=unpack('release/native-post70/smoke/weights.bin')
np.concatenate([sm,ss]).astype('<f4').tofile(out/'block70-scales.f32');head.astype('<f4').tofile(out/'block70-head.f32')
w1,w2,q,k,v,p,bias,scale,fs,ats=body
np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(out/'block70-ffn.f32')
np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(out/'block70-attention.f32')
