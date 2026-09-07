"""Inspect raw captured linear frames; previews are NOT swapchain screenshots."""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--pid',type=int,required=True);p.add_argument('--request',type=int,default=1);args=p.parse_args()
prefix=f'neural-{args.pid}-request-{args.request}'
frames={};report={'scope':'raw linear frame audit, not independent neural reference or final display acceptance','frames':{}}
for side in ('before','after'):
 path=args.root/f'{prefix}-{side}.f16';raw=path.read_bytes();assert len(raw)==16588800
 a=np.frombuffer(raw,np.float16).reshape(1080,1920,4).astype(np.float32);assert np.isfinite(a).all();frames[side]=a
 c=np.maximum(a[:,:,:3],0)
 # Fixed, shared soft shoulder + sRGB solely to make linear data viewable.
 c=np.where(c<=.75,c,.75+.25*(1-np.exp(-5.770780*(c-.75))))
 c=np.clip(c,0,1);c=np.where(c<=.0031308,c*12.92,1.055*c**(1/2.4)-.055)
 Image.fromarray(np.rint(np.clip(c,0,1)*255).astype(np.uint8)).save(args.root/f'{prefix}-{side}-preview.png')
 report['frames'][side]={'sha256':hashlib.sha256(raw).hexdigest(),'rgb_min':float(a[:,:,:3].min()),'rgb_max':float(a[:,:,:3].max()),'nonzero_rgb':int(np.count_nonzero(a[:,:,:3]))}
a,b=frames['before'],frames['after'];d=np.abs(a[:,:,:3]-b[:,:,:3])
report.update(rgb_different=int(np.count_nonzero(d)),alpha_different=int(np.count_nonzero(a[:,:,3]!=b[:,:,3])),max_abs=float(d.max()),mean_abs=float(d.mean()),independent_verified=False)
(args.root/f'{prefix}-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
