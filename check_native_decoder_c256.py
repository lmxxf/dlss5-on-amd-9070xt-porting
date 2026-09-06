"""Original/CPU ordinary C256 decoder check on the RGB512-derived chain."""
from pathlib import Path
import argparse,json,subprocess,os
import numpy as np
from native_c64_reference import unpack,block
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--block',type=int,choices=range(49,56),required=True);p.add_argument('--shift',type=int,choices=range(4),required=True);p.add_argument('--input',type=Path,required=True);a=p.parse_args()
root=Path('release/native-rgb512')/f'decoder-block{a.block}';root.mkdir(exist_ok=False)
report={'status':'running','block':a.block,'shift':a.shift,'input':str(a.input),'scope':'original/CPU C256, not AMD or runtime shift capture'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    weights=root/'weights.bin'
    subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{a.block}.layer0.layer',str(weights)],check=True,timeout=5)
    px=4 if a.shift&1 else 0;py=4 if a.shift&2 else 0;ww=(32+px+7)//8*8;hh=(32+py+7)//8*8
    env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle',
        '/tmp/dlssnr-cubins/dlssnr-03.cubin',str(weights),str(a.input),str(root/'output.fp8'),str(root/'aux.fp8'),
        'cc_tinlayout_fused_swin_8h_256_8_fp8','32','32',str(ww//8),str(hh//8),'8','7',str(a.shift)],check=True,timeout=20,env=env)
    inverse=np.argsort(np.load('release/native-c256/view/mapping.npz')['cell_output_to_hwc'])
    def decode(path):
        raw=np.fromfile(path,np.uint8);assert raw.size>=262144 and not np.any(raw[262144:]) and not np.any((raw[:262144]&127)==127)
        return e4m3fn(raw[:262144].reshape(-1,4096)[:,inverse]).reshape(8,8,4,4,256).transpose(0,2,1,3,4).reshape(32,32,256)
    x=decode(a.input);target=decode(root/'output.fp8');params=unpack(weights)
    padded=np.pad(x,((py,hh-32-py),(px,ww-32-px),(0,0)))
    tiles=padded.reshape(hh//8,8,ww//8,8,256).transpose(0,2,1,3,4).reshape(-1,64,256)
    got=block(tiles,*params).reshape(hh//8,ww//8,8,8,256).transpose(0,2,1,3,4).reshape(hh,ww,256)[py:py+32,px:px+32]
    error=np.abs(got-target);report.update(values=int(target.size),different=int(np.count_nonzero(error)),max_error=float(error.max()))
    assert report['different']==0,'C256 decoder mismatch'
    ffn,qkv,projection,bias,scales,skip=params
    np.concatenate([ffn[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(root/'ffn.f32')
    np.concatenate([*[m.ravel() for m in qkv],projection.ravel(),bias.ravel(),scales,skip]).astype('<f4').tofile(root/'attention.f32')
    x.tofile(root/'input.f32');target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
