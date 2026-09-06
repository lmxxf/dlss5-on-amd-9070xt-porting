"""Original block65 outview + block4 skip -> native block66 reference."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_upsample66_reference import unpack,upsample
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--shift',type=int,choices=range(4),required=True);a=p.parse_args()
base=Path('release/native-rgb512');root=base/f'upsample66-shift{a.shift}';root.mkdir(exist_ok=False)
assert json.loads((base/'block65-outview-validation.json').read_text())['status']=='pass'
report={'status':'running','shift':a.shift,'output_extent':[256,256,32],
        'scope':'RGB-derived original/CPU block66; runtime shift/skip identity not newly captured'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    main=np.fromfile(base/'block65-outview.fp8',np.uint8);skip=np.fromfile(base/'block4-main.fp8',np.uint8)
    assert not np.any(main[1048576:]) and not np.any(skip[2097152:])
    main[:1048576].tofile(root/'input.fp8');skip[:2097152].tofile(root/'skip.fp8')
    c=np.arange(64);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
    x=e4m3fn(main[:1048576]).reshape(4,128,128,16).transpose(1,2,0,3).reshape(128,128,64)[...,perm]
    basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
    def decode(raw):return e4m3fn(raw[:2097152].reshape(-1,512)[:,mapping]).reshape(64,64,4,4,32).transpose(0,2,1,3,4).reshape(256,256,32)
    skip=decode(skip);x.tofile(root/'input.f32');skip.tofile(root/'skip.f32')
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
        'release/native-upsample66/weights.bin',str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),
        'cc_tinlayout_fused_swin_1h_32_1_upsample_fp8','256','256',str(33 if a.shift&1 else 32),str(33 if a.shift&2 else 32),'1','10',str(a.shift),str(root/'skip.fp8')],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);assert not np.any(raw[2097152:]) and not np.any((raw[:2097152]&127)==127)
    target=decode(raw);got=upsample(x,skip,unpack('release/native-upsample66/weights.bin'),a.shift)
    err=np.abs(got-target);report.update(values=int(target.size),different=int(np.count_nonzero(err)),max_error=float(err.max()))
    assert report['different']==0,'RGB-derived C32 upsample differs'
    target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
