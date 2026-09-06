"""Original RGB-derived block55 outview + block14 skip -> block56."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_upsample48_reference import unpack,upsample
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--shift',type=int,choices=range(4),required=True);p.add_argument('--block62',action='store_true');p.add_argument('--decoder-root',type=Path,default=Path('release/native-rgb512'));p.add_argument('--encoder-root',type=Path,default=Path('release/native-rgb512'));a=p.parse_args()
index,C,size,skip_block,cubin=(62,64,128,8,'01') if a.block62 else (56,128,64,14,'02')
count=size*size*C;main_count=count//2;low=size//2;heads=C//32
base=a.decoder_root;root=base/f'upsample{index}-shift{a.shift}';root.mkdir(parents=True,exist_ok=False)
assert json.loads((base/f'block{index-1}-outview-validation.json').read_text())['status']=='pass'
report={'status':'running','shift':a.shift,'output_extent':[size,size,C],
        'scope':f'RGB-derived original/CPU block{index}; runtime shift/skip identity not newly captured'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    main=np.fromfile(base/f'block{index-1}-outview.fp8',np.uint8);skip=np.fromfile(a.encoder_root/f'block{skip_block}-main.fp8',np.uint8)
    assert not np.any(main[main_count:]) and not np.any(skip[count:])
    main[:main_count].tofile(root/'input.fp8');skip[:count].tofile(root/'skip.fp8')
    c=np.arange(2*C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
    x=e4m3fn(main[:main_count]).reshape(C//8,low,low,16).transpose(1,2,0,3).reshape(low,low,2*C)[...,perm]
    inv=np.argsort(np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc'])
    def decode(raw):return e4m3fn(raw[:count].reshape(-1,16*C)[:,inv]).reshape(size//4,size//4,4,4,C).transpose(0,2,1,3,4).reshape(size,size,C)
    skip=decode(skip);x.tofile(root/'input.f32');skip.tofile(root/'skip.f32')
    weights=root/'weights.bin';subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{index}.layer0.layer',str(weights)],check=True,timeout=5)
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle',f'/tmp/dlssnr-cubins/dlssnr-{cubin}.cubin',str(weights),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),
        f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_upsample_fp8',str(size),str(size),str((size+(4 if a.shift&1 else 0)+7)//8),str((size+(4 if a.shift&2 else 0)+7)//8),str(heads),'9',str(a.shift),str(root/'skip.fp8')],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
    target=decode(raw);got=upsample(x,skip,unpack(weights),a.shift);error=np.abs(got-target)
    report.update(values=int(target.size),different=int(np.count_nonzero(error)),max_error=float(error.max()))
    assert report['different']==0,'RGB-derived block56 differs'
    target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
