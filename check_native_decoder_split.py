"""Verify decoder split records against original CUBIN, stage by stage."""
from pathlib import Path
import argparse,json,os,subprocess
import numpy as np
from native_split_weights import unpack
from native_split_reference import ffwd,attention_window
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--block',type=int,choices=[*range(23,31),*range(40,48)],required=True)
p.add_argument('--shift',type=int,choices=range(4),required=True)
p.add_argument('--input',type=Path,required=True)
p.add_argument('--output-root',type=Path,default=Path('release/native-rgb512'))
p.add_argument('--game-extent',action='store_true')
a=p.parse_args();root=a.output_root/f'decoder-block{a.block}'
width,height=(60,36) if a.game_extent else (16,16)
root.mkdir(parents=True,exist_ok=False)
report={'status':'running','block':a.block,'shift':a.shift,'input':str(a.input),'scope':'original/CPU split stages, not AMD or runtime shift proof'}
def save(): (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    for i in range(4):
        subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',
                        f'block{a.block}.layer{i}.layer',str(root/f'block{a.block}-{i}.weights')],check=True,timeout=5)
    env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
    output=root/'output.fp8'
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-split-global-oracle',str(a.input),str(output),
                    *[str(root/f'block{a.block}-{i}.weights') for i in range(4)],str(width),str(height),str(a.shift),'native-inpview' if a.block==23 else 'native-plain'],
                   env=env,check=True,timeout=20)
    inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
    def decode(path):
        v=np.fromfile(path,np.uint8)
        count=width*height*512
        assert v.size>=count and not np.any(v[count:]) and not np.any((v[:count]&127)==127)
        return e4m3fn(v[:count].reshape(-1,8192)[:,inverse]).reshape(height//4,width//4,4,4,512).transpose(0,2,1,3,4).reshape(height,width,512)
    if a.block==23:
        v=np.fromfile(a.input,np.uint8);count=width*height*512
        assert not v[count:].any() and not np.any((v[:count]&127)==127)
        c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
        x=e4m3fn(v[:count]).reshape(32,height,width,16).transpose(1,2,0,3).reshape(height,width,512)[...,perm]
    else:x=decode(a.input)
    fw,fp,qkv,bias,scales,projection=unpack(root,a.block)
    branch=ffwd(x,fw);feature=F(multiply(branch,fp['matrix'],H(x*fp['skip'])))
    attended=attention_window(feature,qkv,bias,scales,a.shift)
    final=F(multiply(attended,projection['matrix'],H(feature*projection['skip'])))
    checks=[]
    for suffix,predicted in [('.branch',branch),('.ffn',feature),('.attn',attended),('',final)]:
        target=decode(str(output)+suffix);error=np.abs(target-predicted)
        checks.append({'stage':suffix or 'final','different':int(np.count_nonzero(error)),'max_error':float(error.max())})
    report['checks']=checks
    assert all(c['different']==0 for c in checks),'native decoder split stage mismatch'
    np.concatenate([fw[k].ravel() for k in ('pre','expand','contract')]).astype('<f4').tofile(root/'ffwd.f32')
    np.concatenate([fp['matrix'].ravel(),fp['skip']]).astype('<f4').tofile(root/'ffwd-projection.f32')
    np.concatenate([*[m.ravel() for m in qkv],projection['matrix'].ravel(),bias.ravel(),scales,projection['skip']]).astype('<f4').tofile(root/'attention.f32')
    x.tofile(root/'input.f32');decode(output).tofile(root/'oracle-0.f32')
    report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
