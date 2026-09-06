"""Original block56 actual240x144 random input comparison."""
from pathlib import Path
import subprocess,json,argparse
import numpy as np
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
from native_upsample48_reference import unpack,upsample
p=argparse.ArgumentParser();p.add_argument('--main-hwc',type=Path);p.add_argument('--output-root',type=Path,default=Path('release/native-upsample56/game'));args=p.parse_args()
root=args.output_root;root.mkdir(parents=True,exist_ok=False)
rng=np.random.default_rng(3014);x=F(rng.normal(0,.25,(72,120,256)).astype(np.float32));skip=F(rng.normal(0,.25,(144,240,128)).astype(np.float32))
if args.main_hwc:
 x=np.fromfile(args.main_hwc,np.float32).reshape(72,120,256)
 assert np.isfinite(x).all() and np.array_equal(F(x),x)
c=np.arange(256);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
quantize(x[...,perm]).reshape(72,120,16,16).transpose(2,0,1,3).copy().tofile(root/'input.fp8')
inv=np.argsort(np.load('release/native-c128/view/mapping.npz')['cell_output_to_hwc'])
cells=quantize(skip).reshape(36,4,60,4,128).transpose(0,2,1,3,4).reshape(-1,2048);packed=np.empty_like(cells);packed[:,inv]=cells;packed.tofile(root/'skip.fp8')
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block56.layer0.layer',str(root/'weights.bin')],check=True,capture_output=True)
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-02.cubin',str(root/'weights.bin'),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),'cc_tinlayout_fused_swin_4h_128_4_upsample_fp8','240','144','31','18','4','9','1',str(root/'skip.fp8')],check=True,timeout=20)
raw=np.fromfile(root/'output.fp8',np.uint8);n=144*240*128
actual=e4m3fn(raw[:n].reshape(-1,2048)[:,inv]).reshape(36,60,4,4,128).transpose(0,2,1,3,4).reshape(skip.shape)
expected=upsample(x,skip,unpack(root/'weights.bin'),1)
report={'scope':'original/CPU block56; independent random skip, optional upstream main; not full RGB chain','main_source':str(args.main_hwc) if args.main_hwc else 'random seed3014','different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'tail_zero':not bool(raw[n:].any())}
print(json.dumps(report,indent=2));(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['different']==0 and report['finite'] and report['tail_zero']
x.tofile(root/'input.f32');skip.tofile(root/'skip.f32');actual.tofile(root/'oracle.f32')
