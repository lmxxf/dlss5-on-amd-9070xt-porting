"""Offline accumulator-alignment hypotheses on the retained 512 post fixture."""
from pathlib import Path
import json
import numpy as np
from native_post70_reference import unpack,aligned
from native_c32_reference import block,H
root=Path('release/native-post70/reference-512-2843');n=512
x=np.fromfile(root/'main-hwc.f32','<f4').reshape(256,256,32);skip=np.fromfile(root/'skip-hwc.f32','<f4').reshape(n,n,32)
color=np.fromfile(root/'color.f32','<f4').reshape(n*n,4)[:,:3];target=np.fromfile(root/'output.f32','<f4').reshape(n*n,4)[:,:3]
body,sm,ss,head=unpack('release/native-post70/smoke/weights.bin')
merged=H(H(np.repeat(np.repeat(x,2,0),2,1)*sm)+skip*ss)
features=block(merged.reshape(64,8,64,8,32).transpose(0,2,1,3,4).reshape(-1,64,32),body,raw_output=True).reshape(64,64,8,8,32).transpose(0,2,1,3,4).reshape(n*n,32)
initial=aligned(features[:,:16],head[:,:16],np.zeros((n*n,3),np.float32));checks=[]
base=np.float32(color.astype(np.float64)*.125-.0625)
for offset in (None,0,1):
    for trunc in (False,True):
        value=aligned(features[:,16:],head[:,16:],initial,offset,trunc)
        encoded=np.float32(value.astype(np.float64)*.03125+base.astype(np.float64));got=np.clip(np.float32(encoded.astype(np.float64)*8+.5),0,1)
        err=np.abs(got-target);checks.append({'acc_exponent_offset':offset,'truncate_acc':trunc,'different':int(np.count_nonzero(err)),'max_error':float(err.max())})
report={'scope':'offline post70 accumulator candidates, not acceptance','checks':checks}
(root/'accumulator-candidates.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
