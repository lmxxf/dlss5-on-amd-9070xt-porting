"""Connect original block47 outview and block22 main to original block48."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_upsample48_reference import unpack,upsample
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--shift',type=int,choices=range(4),required=True);a=p.parse_args()
base=Path('release/native-rgb512');root=base/f'upsample48-shift{a.shift}';root.mkdir(exist_ok=False)
assert json.loads((base/'block47-outview-validation.json').read_text())['status']=='pass'
report={'status':'running','shift':a.shift,'scope':'RGB-derived original/CPU block48; runtime shift/skip identity not newly captured',
        'main_global':'swap','skip_control':False,'projection_control':False,'output_extent':[32,32,256]}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    main=np.fromfile(base/'block47-outview.fp8',np.uint8);skip=np.fromfile(base/'block22-main.fp8',np.uint8)
    assert not np.any(main[131072:]) and not np.any(skip[262144:])
    main[:131072].tofile(root/'input.fp8');skip[:262144].tofile(root/'skip.fp8')
    c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
    x=e4m3fn(main[:131072]).reshape(32,16,16,16).transpose(1,2,0,3).reshape(16,16,512)[...,perm]
    inverse=np.argsort(np.load('release/native-c256/view/mapping.npz')['cell_output_to_hwc'])
    def decode(raw):return e4m3fn(raw[:262144].reshape(-1,4096)[:,inverse]).reshape(8,8,4,4,256).transpose(0,2,1,3,4).reshape(32,32,256)
    skip=decode(skip);x.tofile(root/'input.f32');skip.tofile(root/'skip.f32')
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle',
        '/tmp/dlssnr-cubins/dlssnr-03.cubin','release/native-upsample48/block48.weights',str(root/'input.fp8'),
        str(root/'output.fp8'),str(root/'skip-copy.fp8'),'cc_tinlayout_fused_swin_8h_256_8_upsample_fp8',
        '32','32',str(5 if a.shift&1 else 4),str(5 if a.shift&2 else 4),'8','9',str(a.shift),str(root/'skip.fp8')],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8)
    assert not np.any(raw[262144:]) and not np.any((raw[:262144]&127)==127)
    target=decode(raw);got=upsample(x,skip,unpack('release/native-upsample48/block48.weights'),a.shift)
    error=np.abs(target-got);report.update(values=int(target.size),different=int(np.count_nonzero(error)),max_error=float(error.max()))
    assert report['different']==0,'RGB-derived block48 differs'
    target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
