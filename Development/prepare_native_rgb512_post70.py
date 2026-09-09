"""Original RGB512-derived block69 + preblock skip + base -> final RGB."""
from pathlib import Path
import json,subprocess,argparse
import numpy as np
from native_post70_reference import unpack,post
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--decoder-root',type=Path,default=Path('release/native-rgb512'));p.add_argument('--encoder-root',type=Path,default=Path('release/native-rgb512'));a=p.parse_args()
base=a.decoder_root;root=base/'post70';root.mkdir(parents=True,exist_ok=False)
assert json.loads((base/'block69-outview-validation.json').read_text())['status']=='pass'
report={'status':'running','scope':'original/CPU RGB512-derived final RGB, mask1 mode1; not game contract acceptance'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    main=np.fromfile(base/'block69-outview.fp8',np.uint8);skip=np.fromfile(a.encoder_root/'block0-main.fp8',np.uint8)
    assert not np.any(main[2097152:]) and skip.size==8388608
    main[:2097152].tofile(root/'main.fp8');skip.tofile(root/'skip.fp8')
    x=e4m3fn(main[:2097152]).reshape(2,256,256,16).transpose(1,2,0,3).reshape(256,256,32)
    basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
    skip=e4m3fn(skip.reshape(-1,512)[:,mapping]).reshape(128,128,4,4,32).transpose(0,2,1,3,4).reshape(512,512,32)
    color=np.fromfile(a.encoder_root/'input-hwc.rgba32f','<f4').reshape(512,512,4)
    x.tofile(root/'main.f32');skip.tofile(root/'skip.f32');color.tofile(root/'color.f32')
    weights=Path('release/native-post70/smoke')
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
        'cc_tinlayout_fused_post_block_swin_1h_32_fp8',str(root/'main.fp8'),str(root/'skip.fp8'),str(weights/'weights.bin'),str(weights/'blend.bin'),str(root/'color.f32'),str(root/'output.f32'),'512','512','1','1','0.03125','native'],check=True,timeout=20)
    target=np.fromfile(root/'output.f32','<f4');assert target.size==512*512*4 and np.isfinite(target).all();target=target.reshape(512,512,4)[:,:,:3]
    got=post(x,skip,color[:,:,:3],unpack(weights/'weights.bin'));err=np.abs(got-target)
    report.update(values=int(target.size),different=int(np.count_nonzero(err)),max_error=float(err.max()))
    assert report['different']==0,'RGB-derived final post mismatch'
    target.copy().tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
