"""Original/CPU ordinary C256 decoder check on the RGB512-derived chain."""
from pathlib import Path
import argparse,json,subprocess,os
import numpy as np
from native_c64_reference import unpack,block
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--block',type=int,choices=[*range(49,56),*range(57,62),*range(63,66)],required=True);p.add_argument('--shift',type=int,choices=range(4),required=True);p.add_argument('--input',type=Path,required=True);p.add_argument('--output-root',type=Path,default=Path('release/native-rgb512'));a=p.parse_args()
C,size,cubin=(256,32,'03') if a.block<56 else (128,64,'02') if a.block<62 else (64,128,'01');count=size*size*C;heads=C//32
root=a.output_root/f'decoder-block{a.block}';root.mkdir(parents=True,exist_ok=False)
report={'status':'running','block':a.block,'shift':a.shift,'input':str(a.input),'channels':C,'scope':'original/CPU decoder, not AMD or runtime shift capture'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    weights=root/'weights.bin'
    subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{a.block}.layer0.layer',str(weights)],check=True,timeout=5)
    px=4 if a.shift&1 else 0;py=4 if a.shift&2 else 0;ww=(size+px+7)//8*8;hh=(size+py+7)//8*8
    env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle',
        f'/tmp/dlssnr-cubins/dlssnr-{cubin}.cubin',str(weights),str(a.input),str(root/'output.fp8'),str(root/'aux.fp8'),
        f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_fp8',str(size),str(size),str(ww//8),str(hh//8),str(heads),'7',str(a.shift)],check=True,timeout=20,env=env)
    inverse=np.argsort(np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc'])
    def decode(path):
        raw=np.fromfile(path,np.uint8);assert raw.size>=count and not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
        return e4m3fn(raw[:count].reshape(-1,16*C)[:,inverse]).reshape(size//4,size//4,4,4,C).transpose(0,2,1,3,4).reshape(size,size,C)
    x=decode(a.input);target=decode(root/'output.fp8');params=unpack(weights)
    padded=np.pad(x,((py,hh-size-py),(px,ww-size-px),(0,0)))
    tiles=padded.reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
    got=block(tiles,*params).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(hh,ww,C)[py:py+size,px:px+size]
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
