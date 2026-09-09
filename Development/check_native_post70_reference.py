"""Compare original post70 against the reference with spatial features/color."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_post70_reference import unpack,post
from native_c32_reference import F
from encode_tinlayout_global import quantize
p=argparse.ArgumentParser();p.add_argument('--size',type=int,choices=[16,64,512],default=64);p.add_argument('--seed',type=int,default=2833);a=p.parse_args()
base=Path('release/native-post70');root=base/f'reference-{a.size}-{a.seed}';root.mkdir(exist_ok=False)
report={'status':'running','size':a.size,'seed':a.seed,'scope':'original/CPU post70 mask1 mode1 only; not AMD/game'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    n=a.size;h=n//2;rng=np.random.default_rng(a.seed)
    main=F(rng.normal(0,.25,(h,h,32)).astype(np.float32));skip=F(rng.normal(0,.25,(n,n,32)).astype(np.float32))
    quantize(main).reshape(h,h,2,16).transpose(2,0,1,3).copy().tofile(root/'main.fp8')
    basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
    cells=quantize(skip).reshape(n//4,4,n//4,4,32).transpose(0,2,1,3,4).reshape(-1,512);packed=np.empty_like(cells);packed[:,mapping]=cells;packed.tofile(root/'skip.fp8')
    yy,xx=np.indices((n,n),dtype=np.float32);color=np.stack([.125+xx/(2*n),.125+yy/(2*n),.125+(xx+yy)/(4*n)],axis=-1)
    np.concatenate([color,np.ones((n,n,1),np.float32)],axis=-1).tofile(root/'color.f32')
    main.tofile(root/'main-hwc.f32');skip.tofile(root/'skip-hwc.f32')
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','cc_tinlayout_fused_post_block_swin_1h_32_fp8',str(root/'main.fp8'),str(root/'skip.fp8'),str(base/'smoke/weights.bin'),str(base/'smoke/blend.bin'),str(root/'color.f32'),str(root/'output.f32'),str(n),str(n),'1','1','0.03125','native'],check=True,timeout=20)
    target=np.fromfile(root/'output.f32','<f4');assert target.size==n*n*4 and np.isfinite(target).all();target=target.reshape(n,n,4)[:,:,:3]
    got=post(main,skip,color,unpack(base/'smoke/weights.bin'));err=np.abs(got-target)
    report.update(values=int(target.size),different=int(np.count_nonzero(err)),max_error=float(err.max()))
    assert report['different']==0,'post70 reference mismatch'
    report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
