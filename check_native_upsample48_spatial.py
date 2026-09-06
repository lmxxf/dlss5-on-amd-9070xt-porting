"""Bounded original/CPU block48 spatial test; not AMD acceptance."""
from pathlib import Path
import argparse,json,subprocess,os
import numpy as np
from native_upsample48_reference import unpack,upsample
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--seed',type=int,default=2401);p.add_argument('--shift',type=int,choices=range(4),default=0);p.add_argument('--main-banks',action='store_true');p.add_argument('--main-global',choices=['plain','swap']);controls=p.add_mutually_exclusive_group();controls.add_argument('--skip-control',action='store_true');controls.add_argument('--projection-control',action='store_true');a=p.parse_args()
if a.main_banks and a.main_global:p.error('choose bank or global layout')
base=Path('release/native-upsample48');root=base/f'spatial-{a.seed}-{a.shift}{"-skip-control" if a.skip_control else "-projection-control" if a.projection_control else ""}{"-banks" if a.main_banks else ""}{"-global-"+a.main_global if a.main_global else ""}';root.mkdir(exist_ok=False)
report={'status':'running','seed':a.seed,'shift':a.shift,'skip_control':a.skip_control,'projection_control':a.projection_control,'scope':'original/CPU block48 spatial 16x16 output only'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    rng=np.random.default_rng(a.seed);x=F(rng.normal(0,.25,(8,8,512)).astype(np.float32));skip=F(rng.normal(0,.25,(16,16,256)).astype(np.float32))
    def mapping(c):return np.argsort(np.load(f'release/native-c{c}/'+('split-view' if c==512 else 'view')+'/mapping.npz')['cell_output_to_hwc'])
    def encode(v,path):
        h,w,c=v.shape;cells=quantize(v).reshape(h//4,4,w//4,4,c).transpose(0,2,1,3,4).reshape(-1,16*c)
        packed=np.empty_like(cells);packed[:,mapping(c)]=cells;packed.tofile(path)
    encode(x,root/'input.fp8');encode(skip,root/'skip.fp8');x.tofile(root/'input.f32');skip.tofile(root/'skip.f32')
    report['main_banks']=a.main_banks
    report['main_global']=a.main_global
    if a.main_global:
        c=np.arange(512);order=((c&~3)|((c&1)<<1)|((c&2)>>1)) if a.main_global=='swap' else c
        quantize(x[...,order]).reshape(8,8,32,16).transpose(2,0,1,3).copy().tofile(root/'input.fp8')
    if a.main_banks:
        cells=quantize(x).reshape(2,4,2,4,2,256).transpose(0,2,4,1,3,5).reshape(-1,4096)
        packed=np.empty_like(cells);packed[:,mapping(256)]=cells;packed.tofile(root/'input.fp8')
    weights=base/('skip-control.weights' if a.skip_control else 'block48.weights')
    if a.projection_control:
        from native_split_reference import bits
        control=np.fromfile(base/'skip-control.weights',np.uint8)
        control[0x78200:0x78400]=0
        oi=bits(131072,[3,6,7,8,9,10,11,12]);ii=bits(131072,[1,0,4,5,2,13,14,15,16])
        control[0x58000:0x78000]=np.where(oi==ii,0x38,0).astype(np.uint8)
        weights=root/'projection-control.weights';control.tofile(weights)
    env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',
        str(weights),str(root/'input.fp8'),str(root/'output.fp8'),str(root/'skip-copy.fp8'),
        'cc_tinlayout_fused_swin_8h_256_8_upsample_fp8','16','16',str(3 if a.shift&1 else 2),str(3 if a.shift&2 else 2),'8','9',str(a.shift),str(root/'skip.fp8')],env=env,check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);assert not np.any(raw[65536:]) and not np.any((raw[:65536]&127)==127)
    target=e4m3fn(raw[:65536].reshape(-1,4096)[:,mapping(256)]).reshape(4,4,4,4,256).transpose(0,2,1,3,4).reshape(16,16,256)
    got=skip if a.skip_control else np.repeat(np.repeat(x[:,:,:256],2,0),2,1) if a.projection_control else upsample(x,skip,unpack(base/'block48.weights'),a.shift);err=np.abs(got-target)
    report.update(different=int(np.count_nonzero(err)),max_error=float(err.max()),values=int(target.size))
    assert report['different']==0,'block48 spatial mismatch'
    target.tofile(root/'oracle.f32');report['status']='pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
