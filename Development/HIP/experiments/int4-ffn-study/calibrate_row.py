from pathlib import Path
import sys,json
import numpy as np
from common import fp8,activate,metric
root,wpath,out=map(Path,sys.argv[1:4]);w=fp8(np.fromfile(wpath,'<f2').astype(np.float32).reshape(4096,1024));s=np.load('/tmp/int4-block31-calibration.npz')['smooth']
t=np.arange(400);r=(t&~15)|((t&1)<<3)|((t&14)>>1);valid=np.flatnonzero(r<375)
cal=np.fromfile(root/'capture-900-s0/features/1-input.f32','<f4').reshape(400,1024)[valid];cal=fp8(cal);ref=activate(cal@w.T)
def quant(a,clip):
 scale=np.maximum(np.abs(a).max(1,keepdims=True)*np.float32(clip),1.e-10)/np.float32(7)
 return np.clip(np.rint(a/scale),-7,7)*scale
rows=[]
for clip in [.5,.6,.75,.9,1.]:
 a=activate(quant(cal/s,clip)@quant(w*s,clip).T);score=metric(a,ref);rows.append(dict(clip=clip,**score));print(clip,score['relative_l1'],flush=True)
best=min(rows,key=lambda r:r['relative_rmse'])['clip'];tests=[]
for seq,frame in [(1,1),(1,5),(1,9),(2,1),(2,5),(2,9),(3,6),(3,9),(4,6),(4,9)]:
 x=fp8(np.fromfile(root/f'capture-900-s{seq}/features/{frame+1}-input.f32','<f4').reshape(400,1024)[valid[::6]])
 y=activate(quant(x/s,best)@quant(w*s,best).T);tests.append(dict(sequence=seq,frame=frame,**metric(y,activate(x@w.T))))
result=dict(selected_clip=best,calibration=rows,evaluation=tests,selection='min post-activation relative RMSE on sequence0 frame0, all375 valid tokens; transformations for evaluation only')
(out/'row-calibration.json').write_text(json.dumps(result,indent=2)+'\n');print('selected',best,'eval relative L1',np.mean([r['relative_l1'] for r in tests]))
