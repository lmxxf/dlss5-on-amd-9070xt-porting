"""Small original/CPU block56 test, with independently extracted weights."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_upsample48_reference import unpack,upsample
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--size',type=int,choices=[16,64,128],default=16);p.add_argument('--block62',action='store_true');p.add_argument('--seed',type=int,default=2501);p.add_argument('--shift',type=int,choices=range(4),default=0);a=p.parse_args()
C,index,cubin=(64,62,'01') if a.block62 else (128,56,'02');heads=C//32
base=Path(f'release/native-upsample{index}');base.mkdir(exist_ok=True);root=base/f'spatial-{a.size}-{a.seed}-{a.shift}';root.mkdir(exist_ok=False)
report={'status':'running','output_extent':[a.size,a.size,C],'seed':a.seed,'shift':a.shift,'scope':f'original/CPU block{index}, not AMD/game'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    weights=root/'weights.bin';subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{index}.layer0.layer',str(weights)],check=True,timeout=5)
    n=a.size;h=n//2;rng=np.random.default_rng(a.seed)
    x=F(rng.normal(0,.25,(h,h,2*C)).astype(np.float32));skip=F(rng.normal(0,.25,(n,n,C)).astype(np.float32))
    c=np.arange(2*C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
    quantize(x[...,perm]).reshape(h,h,C//8,16).transpose(2,0,1,3).copy().tofile(root/'input.fp8')
    inv=np.argsort(np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc'])
    cells=quantize(skip).reshape(n//4,4,n//4,4,C).transpose(0,2,1,3,4).reshape(-1,16*C);packed=np.empty_like(cells);packed[:,inv]=cells;packed.tofile(root/'skip.fp8')
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle',f'/tmp/dlssnr-cubins/dlssnr-{cubin}.cubin',str(weights),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),
        f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_upsample_fp8',str(n),str(n),str((n+(4 if a.shift&1 else 0)+7)//8),str((n+(4 if a.shift&2 else 0)+7)//8),str(heads),'9',str(a.shift),str(root/'skip.fp8')],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);count=n*n*C
    assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
    target=e4m3fn(raw[:count].reshape(-1,16*C)[:,inv]).reshape(n//4,n//4,4,4,C).transpose(0,2,1,3,4).reshape(n,n,C)
    got=upsample(x,skip,unpack(weights),a.shift);error=np.abs(target-got)
    report.update(values=count,different=int(np.count_nonzero(error)),max_error=float(error.max()))
    assert report['different']==0,'block56 differs'
    x.tofile(root/'input.f32');skip.tofile(root/'skip.f32');target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
