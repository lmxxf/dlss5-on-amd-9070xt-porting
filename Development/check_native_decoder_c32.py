"""Original/CPU C32 decoder67..69 using RGB512-derived inputs."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_c32_reference import unpack,block
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--block',type=int,choices=[67,68,69],required=True);p.add_argument('--shift',type=int,choices=range(4),required=True);p.add_argument('--input',type=Path,required=True);p.add_argument('--output-root',type=Path,default=Path('release/native-rgb512'));p.add_argument('--game-extent',action='store_true');a=p.parse_args()
width,height=(960,576) if a.game_extent else (256,256);count=width*height*32
root=a.output_root/f'decoder-block{a.block}';root.mkdir(parents=True,exist_ok=False)
report={'status':'running','block':a.block,'shift':a.shift,'input':str(a.input),'scope':'original/CPU C32, not AMD or runtime shift capture'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    weights=root/'weights.bin';subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{a.block}.layer0.layer',str(weights)],check=True,timeout=5)
    px=4 if a.shift&1 else 0;py=4 if a.shift&2 else 0;ww=(width+px+7)//8*8;hh=(height+py+7)//8*8
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(weights),str(a.input),str(root/'output.fp8'),str(root/'aux.fp8'),
        'cc_tinlayout_fused_swin_1h_32_1_fp8',str(width),str(height),str(ww//8),str(hh//8),'1','4',str(a.shift)],check=True,timeout=20)
    basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
    def decode(path):
        raw=np.fromfile(path,np.uint8);assert raw.size>=count and not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
        return e4m3fn(raw[:count].reshape(-1,512)[:,mapping]).reshape(height//4,width//4,4,4,32).transpose(0,2,1,3,4).reshape(height,width,32)
    x=decode(a.input);target=decode(root/'output.fp8');params=unpack(weights)
    padded=np.pad(x,((py,hh-height-py),(px,ww-width-px),(0,0)))
    tiles=padded.reshape(hh//8,8,ww//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
    got=block(tiles,params).reshape(hh//8,ww//8,8,8,32).transpose(0,2,1,3,4).reshape(hh,ww,32)[py:py+height,px:px+width]
    err=np.abs(got-target);report.update(values=int(target.size),different=int(np.count_nonzero(err)),max_error=float(err.max()))
    assert report['different']==0,'C32 decoder differs'
    w1,w2,q,k,v,pw,bias,scale,fs,ats=params
    np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(root/'ffn.f32')
    np.concatenate([q.ravel(),k.ravel(),v.ravel(),pw.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(root/'attention.f32')
    x.tofile(root/'input.f32');target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
