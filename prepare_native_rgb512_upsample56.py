"""Original RGB-derived block55 outview + block14 skip -> block56."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_upsample48_reference import unpack,upsample
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--shift',type=int,choices=range(4),required=True);a=p.parse_args()
base=Path('release/native-rgb512');root=base/f'upsample56-shift{a.shift}';root.mkdir(exist_ok=False)
assert json.loads((base/'block55-outview-validation.json').read_text())['status']=='pass'
report={'status':'running','shift':a.shift,'output_extent':[64,64,128],
        'scope':'RGB-derived original/CPU block56; runtime shift/skip identity not newly captured'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    main=np.fromfile(base/'block55-outview.fp8',np.uint8);skip=np.fromfile(base/'block14-main.fp8',np.uint8)
    assert not np.any(main[262144:]) and not np.any(skip[524288:])
    main[:262144].tofile(root/'input.fp8');skip[:524288].tofile(root/'skip.fp8')
    c=np.arange(256);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
    x=e4m3fn(main[:262144]).reshape(16,32,32,16).transpose(1,2,0,3).reshape(32,32,256)[...,perm]
    inv=np.argsort(np.load('release/native-c128/view/mapping.npz')['cell_output_to_hwc'])
    def decode(raw):return e4m3fn(raw[:524288].reshape(-1,2048)[:,inv]).reshape(16,16,4,4,128).transpose(0,2,1,3,4).reshape(64,64,128)
    skip=decode(skip);x.tofile(root/'input.f32');skip.tofile(root/'skip.f32')
    weights=root/'weights.bin';subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block56.layer0.layer',str(weights)],check=True,timeout=5)
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-02.cubin',str(weights),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),
        'cc_tinlayout_fused_swin_4h_128_4_upsample_fp8','64','64',str(9 if a.shift&1 else 8),str(9 if a.shift&2 else 8),'4','9',str(a.shift),str(root/'skip.fp8')],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);assert not np.any(raw[524288:]) and not np.any((raw[:524288]&127)==127)
    target=decode(raw);got=upsample(x,skip,unpack(weights),a.shift);error=np.abs(got-target)
    report.update(values=int(target.size),different=int(np.count_nonzero(error)),max_error=float(error.max()))
    assert report['different']==0,'RGB-derived block56 differs'
    target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
