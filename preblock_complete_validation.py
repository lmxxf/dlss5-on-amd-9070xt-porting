"""Compare the composed AMD preblock's raw FP16 result to original main and DS."""
import argparse,json
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args()
w=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);pm=np.argmax(np.abs(w),axis=0)
raw=np.fromfile(a.folder/'amd-raw.f32','<f4').reshape(-1,8,8,32);assert np.isfinite(raw).all()
main=e4m3fn(quantize(raw));target=e4m3fn(np.fromfile(a.folder/'main.fp8',np.uint8).reshape(-1,2048)[:,pm]).reshape(main.shape)
y,x,c=np.indices((4,4,32));di=(c//16)*256+(y*4+x)*16+c%16
ds=e4m3fn(np.fromfile(a.folder/'ds.fp8',np.uint8).reshape(-1,512)[:,di])
pooled=e4m3fn(quantize(raw.reshape(-1,4,2,4,2,32).mean((2,4)).astype(np.float16).astype(np.float32)))
def metrics(got,want):
 return {'correlation':float(np.corrcoef(got.ravel(),want.ravel())[0,1]),'exact_fraction':float(np.mean(got==want)),'mae':float(np.abs(got-want).mean()),'max_error':float(np.abs(got-want).max())}
print(json.dumps({'tiles':len(raw),'main':metrics(main,target),'downsample':metrics(pooled,ds),'scope':'composed preblock lab control, not final game image acceptance'},indent=2))
