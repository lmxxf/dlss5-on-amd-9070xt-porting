"""Original/CPU C32 upsample spatial comparison, not AMD/game acceptance."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_upsample66_reference import unpack,upsample
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--size',type=int,choices=[16,256],default=16);p.add_argument('--seed',type=int,default=2701);p.add_argument('--shift',type=int,choices=range(4),default=0);a=p.parse_args()
base=Path('release/native-upsample66');root=base/f'spatial-{a.size}-{a.seed}-{a.shift}';root.mkdir(exist_ok=False)
report={'status':'running','output_extent':[a.size,a.size,32],'seed':a.seed,'shift':a.shift,'scope':'original/CPU block66 only'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    n=a.size;h=n//2;rng=np.random.default_rng(a.seed);x=F(rng.normal(0,.25,(h,h,64)).astype(np.float32));skip=F(rng.normal(0,.25,(n,n,32)).astype(np.float32))
    c=np.arange(64);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
    quantize(x[...,perm]).reshape(h,h,4,16).transpose(2,0,1,3).copy().tofile(root/'input.fp8')
    basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
    cells=quantize(skip).reshape(n//4,4,n//4,4,32).transpose(0,2,1,3,4).reshape(-1,512);packed=np.empty_like(cells);packed[:,mapping]=cells;packed.tofile(root/'skip.fp8')
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(base/'weights.bin'),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),
        'cc_tinlayout_fused_swin_1h_32_1_upsample_fp8',str(n),str(n),str((n+(4 if a.shift&1 else 0)+7)//8),str((n+(4 if a.shift&2 else 0)+7)//8),'1','10',str(a.shift),str(root/'skip.fp8')],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);count=n*n*32
    assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
    target=e4m3fn(raw[:count].reshape(-1,512)[:,mapping]).reshape(n//4,n//4,4,4,32).transpose(0,2,1,3,4).reshape(n,n,32)
    got=upsample(x,skip,unpack(base/'weights.bin'),a.shift);error=np.abs(got-target)
    report.update(values=count,different=int(np.count_nonzero(error)),max_error=float(error.max()))
    assert report['different']==0,'C32 spatial mismatch'
    x.tofile(root/'input.f32');skip.tofile(root/'skip.f32');target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
