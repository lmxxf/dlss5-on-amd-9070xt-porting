"""Original actual960x576 C32 upsample random fixture."""
from pathlib import Path
import subprocess,json,argparse
import numpy as np
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
from native_upsample66_reference import unpack,upsample
p=argparse.ArgumentParser();p.add_argument('--main-hwc',type=Path);p.add_argument('--output-root',type=Path,default=Path('release/native-upsample66/game'));args=p.parse_args()
root=args.output_root;root.mkdir(parents=True,exist_ok=False)
rng=np.random.default_rng(3016);x=F(rng.normal(0,.25,(288,480,64)).astype(np.float32));skip=F(rng.normal(0,.25,(576,960,32)).astype(np.float32))
if args.main_hwc:
 x=np.fromfile(args.main_hwc,np.float32).reshape(288,480,64)
 assert np.isfinite(x).all() and np.array_equal(F(x),x)
c=np.arange(64);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
quantize(x[...,perm]).reshape(288,480,4,16).transpose(2,0,1,3).copy().tofile(root/'input.fp8')
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
cells=quantize(skip).reshape(144,4,240,4,32).transpose(0,2,1,3,4).reshape(-1,512);packed=np.empty_like(cells);packed[:,mapping]=cells;packed.tofile(root/'skip.fp8')
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-upsample66/weights.bin',str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),'cc_tinlayout_fused_swin_1h_32_1_upsample_fp8','960','576','120','72','1','10','0',str(root/'skip.fp8')],check=True,timeout=20)
raw=np.fromfile(root/'output.fp8',np.uint8);n=576*960*32
actual=e4m3fn(raw[:n].reshape(-1,512)[:,mapping]).reshape(144,240,4,4,32).transpose(0,2,1,3,4).reshape(skip.shape)
expected=upsample(x,skip,unpack('release/native-upsample66/weights.bin'),0)
r={'scope':'original/CPU block66 with independent random skip; not full RGB chain','main_source':str(args.main_hwc) if args.main_hwc else 'random seed3016','different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'tail_zero':not bool(raw[n:].any())}
print(json.dumps(r,indent=2));(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n')
assert r['different']==0 and r['finite'] and r['tail_zero']
x.tofile(root/'input.f32');skip.tofile(root/'skip.f32');actual.tofile(root/'oracle.f32')
