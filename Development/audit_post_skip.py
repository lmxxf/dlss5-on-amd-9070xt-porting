"""Independent full-bank post skip test against the original NVIDIA CUBIN."""
import argparse,json
from pathlib import Path
import numpy as np
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--compare',action='store_true');p.add_argument('--basis',action='store_true');p.add_argument('--matrix',type=Path,default=Path('block70-prefix-global-skip-effective.bin'));a=p.parse_args()
if not a.compare:
 a.folder.mkdir(parents=True,exist_ok=True)
 w=np.fromfile('/tmp/block70.weights','<f2').copy();assert w.size==10904
 # Expose only the original five-op prefix; keep original prefix parameters.
 w[:4096]=0;w[4160:4192]=1;w[4200:5736]=0;w[9840:10352]=0;w[10352:10384]=1
 w.tofile(a.folder/'prefix-control.weights')
 rng=np.random.default_rng(29771)
 if a.basis:
  h=np.ones((1,1),np.float32)
  while len(h)<2048:h=np.block([[h,h],[h,-h]])
  x=quantize(h*0.5)
 else:x=quantize(rng.uniform(-4,4,(64,2048)).astype(np.float32))
 records=np.zeros((len(x),2560),np.uint8);records[:,:2048]=x
 records.tofile(a.folder/'records.u8');e4m3fn(x).tofile(a.folder/'input.f32')
 print(f'{len(x)} samples, two complete 1024-byte banks each')
else:
 x=np.fromfile(a.folder/'input.f32','<f4').reshape(-1,2048)
 w=np.fromfile(a.matrix,'<f4').reshape(2048,2048)
 want=np.fromfile(a.folder/'oracle.f32','<f4').reshape(-1,2048)
 got=x@w
 e=got-want
 report={'samples':len(x),'nonfinite':int((~np.isfinite(want)).sum()),'correlation':float(np.corrcoef(got.ravel(),want.ravel())[0,1]),'mae':float(np.abs(e).mean()),'max_error':float(np.abs(e).max()),'oracle_absmax':float(np.abs(want).max())}
 print(json.dumps(report,indent=2))
 assert report['nonfinite']==0 and report['oracle_absmax']>0 and report['correlation']>0.9999 and report['mae']<0.001
