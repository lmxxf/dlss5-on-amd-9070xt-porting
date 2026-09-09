"""Bounded post70 spatial layout candidates with fixed base RGB; no fitting."""
from pathlib import Path
import json,subprocess,argparse
import numpy as np
from native_c32_reference import unpack_bytes,block,H,F
from native_split_reference import bits
from encode_tinlayout_global import quantize
p=argparse.ArgumentParser();p.add_argument('--seed',type=int,default=2801);a=p.parse_args()
base=Path('release/native-post70');root=base/f'spatial-{a.seed}';root.mkdir(exist_ok=False)
raw=np.fromfile(base/'smoke/weights.bin',np.uint8)
ordinary=np.zeros(20672,np.uint8);ordinary[:0x2050]=raw[:0x2050];ordinary[0x2060:]=raw[0x20d0:0x5130];body=unpack_bytes(ordinary)
order=np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31])
sm=np.empty(32,np.float32);ss=np.empty(32,np.float32);sm[order]=raw[0x2050:0x2090].view('<f2');ss[order]=raw[0x2090:0x20d0].view('<f2')
head=np.empty((16,32),np.float32);head[bits(512,[2,5,6,7]),bits(512,[0,1,3,4,8])]=raw[0x5130:].view('<f2');head=head[[0,2,4]]
rng=np.random.default_rng(a.seed);x=F(rng.normal(0,.25,(8,8,32)).astype(np.float32));skip=F(rng.normal(0,.25,(16,16,32)).astype(np.float32))
x.tofile(root/'main-hwc.f32');skip.tofile(root/'skip-hwc.f32');color=np.full((16,16,4),.25,np.float32);color[:,:,3]=1;color.tofile(root/'color.f32')
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);full=np.argmax(abs(basis),axis=0).reshape(8,8,32);cell=full[:4,:4].ravel()
def pack(v,kind):
    h,w,_=v.shape;v=quantize(v)
    if kind.startswith('global'):
        c=np.arange(32);p=(c&~3)|((c&1)<<1)|((c&2)>>1) if kind=='global-swap' else c
        return v[...,p].reshape(h,w,2,16).transpose(2,0,1,3).copy().ravel()
    if kind=='cell':
        cells=v.reshape(h//4,4,w//4,4,32).transpose(0,2,1,3,4).reshape(-1,512);out=np.empty_like(cells);out[:,cell]=cells;return out.ravel()
    tiles=v.reshape(h//8,8,w//8,8,32).transpose(0,2,1,3,4).reshape(-1,2048);out=np.empty_like(tiles);out[:,full.ravel()]=tiles
    return out.reshape(h//8,w//8,2,1024).transpose(0,2,1,3).copy().ravel()
up=np.repeat(np.repeat(x,2,0),2,1);predictions={}
for rounding in ('both','main-first','skip-first'):
    merged=H(H(up*sm)+H(skip*ss)) if rounding=='both' else H(H(up*sm)+skip*ss) if rounding=='main-first' else H(up*sm+H(skip*ss))
    tiles=merged.reshape(2,8,2,8,32).transpose(0,2,1,3,4).reshape(4,64,32)
    features=block(tiles,body,raw_output=True).reshape(2,2,8,8,32).transpose(0,2,1,3,4).reshape(16,16,32)
    y=H(H(features[:,:,:16]@head[:,:16].T)+features[:,:,16:]@head[:,16:].T)
    predictions[rounding]=np.clip(.25+.25*y,0,1)
checks=[]
for main_layout in ('global','global-swap','cell'):
    for skip_layout in ('cell','preblock'):
        name=f'{main_layout}-{skip_layout}';pack(x,main_layout).tofile(root/f'{name}-main.fp8');pack(skip,skip_layout).tofile(root/f'{name}-skip.fp8')
        output=root/f'{name}-out.f32'
        subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-post70-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','cc_tinlayout_fused_post_block_swin_1h_32_fp8',
            str(root/f'{name}-main.fp8'),str(root/f'{name}-skip.fp8'),str(base/'smoke/weights.bin'),str(base/'smoke/blend.bin'),str(root/'color.f32'),str(output),'16','16','1','1','0.03125','native'],check=True,timeout=20,capture_output=True)
        target=np.fromfile(output,'<f4');assert target.size==1024 and np.isfinite(target).all();target=target.reshape(16,16,4)[:,:,:3]
        for rounding,got in predictions.items():
            err=np.abs(got-target);checks.append({'main_layout':main_layout,'skip_layout':skip_layout,'rounding':rounding,'different':int(np.count_nonzero(err)),'max_error':float(err.max())})
report={'scope':'post70 spatial candidates only','checks':checks,'best':sorted(checks,key=lambda c:c['max_error'])[:5]}
(root/'candidates.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'best':report['best']},indent=2))
