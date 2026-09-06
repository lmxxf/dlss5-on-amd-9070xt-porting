"""Check isolated temporal preblock wiring or single-input regression."""
from pathlib import Path
import argparse,json
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--single',action='store_true');p.add_argument('--direct',action='store_true');p.add_argument('--motion',action='store_true');p.add_argument('--variable',action='store_true');p.add_argument('--gpu-root',type=Path);p.add_argument('--prefix');g=p.add_mutually_exclusive_group();g.add_argument('--rect',action='store_true');g.add_argument('--large',action='store_true');args=p.parse_args()
root=Path('release/native-temporal-inputs-gates');gpu=root/('amd-motion' if args.motion else 'amd-direct' if args.direct else 'amd-preblock')
case='single' if args.single else 'both_shifted'
if args.variable:root=Path('release/native-temporal-variable');gpu=root;case='original'
width,height=(120,72) if args.large else (24,16) if args.rect else (8,8)
if args.rect:root=Path('release/native-temporal-rect');gpu=root;case='single' if args.single else 'original'
if args.large:root=Path('release/native-temporal-large');gpu=root;case='single' if args.single else 'original'
prefix='gpu-single-' if args.single else 'gpu-'
if args.gpu_root:gpu=args.gpu_root
if args.prefix:prefix=args.prefix
basis=np.fromfile('release/post-skip-basis/matrix.f32',np.float32).reshape(2048,2048)
mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
main=np.fromfile(root/f'{case}.main.fp8',np.uint8);down=np.fromfile(root/f'{case}.down.fp8',np.uint8)
assert main.size==width*height*32 and down.size==width*height*8 and not np.any((main&127)==127) and not np.any((down&127)==127)
main=e4m3fn(main.reshape(-1,512)[:,mapping]).reshape(height//4,width//4,4,4,32).transpose(0,2,1,3,4).ravel()
down=e4m3fn(down).reshape(2,height//2,width//2,16).transpose(1,2,0,3).ravel()
checks=[]
for name,expected in [('main',main),('down',down)]:
    actual=np.fromfile(gpu/f'{prefix}{name}.f32',np.float32)
    assert actual.shape==expected.shape and np.isfinite(actual).all()
    checks.append({'branch':name,'values':actual.size,'different':int(np.count_nonzero(actual!=expected))})
report={'scope':'single input regression' if args.single else 'GPU motion coordinates -> sampler -> preblock; controlled 8x8 no slot18 branch' if args.motion else 'GPU sampler directly into preblock; supplied transformed coordinates, not full motion path' if args.direct else 'CPU-supplied temporal RGB into production GPU preblock; sampler not directly connected',
        'checks':checks,'pass':all(c['different']==0 for c in checks)}
if args.variable:report['scope']='GPU spatially varying motion -> sampler -> preblock; controlled 8x8 no slot18 branch'
if args.rect:report['scope']='GPU single-input preblock regression; controlled 24x16' if args.single else 'GPU spatially varying motion -> sampler -> preblock; controlled 24x16 no slot18 branch'
if args.large:report['scope']='GPU single-input preblock regression; controlled 120x72' if args.single else 'GPU spatially varying motion -> sampler -> preblock; controlled 120x72 no slot18 branch'
(gpu/f'{case}-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert report['pass']
