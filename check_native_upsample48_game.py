"""Original real-size block48 random-input numerical check."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
from native_upsample48_reference import unpack,upsample
root=Path('release/native-upsample48/game');root.mkdir(exist_ok=False)
rng=np.random.default_rng(3013);x=F(rng.normal(0,.25,(36,60,512)).astype(np.float32));skip=F(rng.normal(0,.25,(72,120,256)).astype(np.float32))
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
quantize(x[...,perm]).reshape(36,60,32,16).transpose(2,0,1,3).copy().tofile(root/'input.fp8')
inv=np.argsort(np.load('release/native-c256/view/mapping.npz')['cell_output_to_hwc'])
cells=quantize(skip).reshape(18,4,30,4,256).transpose(0,2,1,3,4).reshape(-1,4096);packed=np.empty_like(cells);packed[:,inv]=cells;packed.tofile(root/'skip.fp8')
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin','release/native-upsample48/block48.weights',str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),'cc_tinlayout_fused_swin_8h_256_8_upsample_fp8','120','72','15','9','8','9','0',str(root/'skip.fp8')],check=True,timeout=20)
raw=np.fromfile(root/'output.fp8',np.uint8);n=72*120*256
actual=e4m3fn(raw[:n].reshape(-1,4096)[:,inv]).reshape(18,30,4,4,256).transpose(0,2,1,3,4).reshape(skip.shape)
expected=upsample(x,skip,unpack('release/native-upsample48/block48.weights'),0)
report={'scope':'original/CPU random block48 actual extent, not upstream chain or AMD','different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'tail_zero':not bool(raw[n:].any())}
print(json.dumps(report,indent=2));(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['different']==0 and report['finite'] and report['tail_zero']
x.tofile(root/'input.f32');skip.tofile(root/'skip.f32');actual.tofile(root/'oracle.f32')
