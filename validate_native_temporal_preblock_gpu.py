"""Check isolated temporal preblock wiring or single-input regression."""
from pathlib import Path
import argparse,json
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--single',action='store_true');p.add_argument('--direct',action='store_true');args=p.parse_args()
root=Path('release/native-temporal-inputs-gates');gpu=root/('amd-direct' if args.direct else 'amd-preblock')
case='single' if args.single else 'both_shifted'
prefix='gpu-single-' if args.single else 'gpu-'
basis=np.fromfile('release/post-skip-basis/matrix.f32',np.float32).reshape(2048,2048)
mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
main=np.fromfile(root/f'{case}.main.fp8',np.uint8);down=np.fromfile(root/f'{case}.down.fp8',np.uint8)
assert main.size==2048 and down.size==512 and not np.any((main&127)==127) and not np.any((down&127)==127)
main=e4m3fn(main.reshape(-1,512)[:,mapping]).reshape(2,2,4,4,32).transpose(0,2,1,3,4).ravel()
down=e4m3fn(down).reshape(2,4,4,16).transpose(1,2,0,3).ravel()
checks=[]
for name,expected in [('main',main),('down',down)]:
    actual=np.fromfile(gpu/f'{prefix}{name}.f32',np.float32)
    assert actual.shape==expected.shape and np.isfinite(actual).all()
    checks.append({'branch':name,'values':actual.size,'different':int(np.count_nonzero(actual!=expected))})
report={'scope':'single input regression' if args.single else 'GPU sampler directly into preblock; supplied transformed coordinates, not full motion path' if args.direct else 'CPU-supplied temporal RGB into production GPU preblock; sampler not directly connected',
        'checks':checks,'pass':all(c['different']==0 for c in checks)}
(gpu/f'{case}-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert report['pass']
