"""Locate post70 GPU divergence using failure-only merged/feature readbacks."""
from pathlib import Path
import json
import numpy as np
from native_post70_reference import unpack,aligned
from native_c32_reference import H,block
root=Path('release/native-post70/amd');n=512
main=np.fromfile(root/'main.f32','<f4').reshape(256,256,32);skip=np.fromfile(root/'skip.f32','<f4').reshape(n,n,32)
body,sm,ss,head=unpack('release/native-post70/smoke/weights.bin')
merged=H(H(np.repeat(np.repeat(main,2,0),2,1)*sm)+skip*ss)
gpu_merged=np.fromfile(root/'gpu-merged.f32','<f4').reshape(n,n,32)
gpu_features=np.fromfile(root/'gpu-features.f32','<f4').reshape(n,n,32)
def features(v):return block(v.reshape(64,8,64,8,32).transpose(0,2,1,3,4).reshape(-1,64,32),body,raw_output=True).reshape(64,64,8,8,32).transpose(0,2,1,3,4).reshape(n,n,32)
def compare(a,b):
    finite=bool(np.isfinite(a).all() and np.isfinite(b).all())
    return {'finite':finite,'different':int(np.count_nonzero(a!=b)),'max_error':float(np.max(abs(a-b))) if finite else None}
report={'merge':compare(gpu_merged,merged),'body_from_gpu_merge':compare(gpu_features,features(gpu_merged))}
f=gpu_features.reshape(-1,32);zero=np.zeros((n*n,3),np.float32)
first=aligned(f[:,:16],head[:,:16],zero);second=aligned(f[:,16:],head[:,16:],zero)
combined=aligned(f[:,16:],head[:,16:],first)
color=np.fromfile(root/'color.f32','<f4').reshape(-1,4)[:,:3]
gpu_rgb=np.fromfile(root/'gpu-after-rgb-fix.f32','<f4').reshape(-1,3)
base=np.float32(color.astype(np.float64)*.125-.0625)
report['head_candidates']={}
for name,value in [('first_only',first),('second_only',second),('combined',combined)]:
    encoded=np.float32(value.astype(np.float64)*.03125+base.astype(np.float64))
    rgb=np.clip(np.float32(encoded.astype(np.float64)*8+.5),0,1)
    report['head_candidates'][name]=compare(gpu_rgb,rgb)
(root/'stage-diagnostic.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
