"""Compare original plain and outview projection outputs in logical HWC."""
from pathlib import Path
import json,argparse
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group();g.add_argument('--block55',action='store_true');g.add_argument('--block61',action='store_true');g.add_argument('--block65',action='store_true');a=p.parse_args()
block,C,size=(65,64,128) if a.block65 else (61,128,64) if a.block61 else (55,256,32) if a.block55 else (47,512,16)
root=Path('release/native-rgb512');count=size*size*C
plain=np.fromfile(root/f'decoder-block{block}/output.fp8',np.uint8)
out=np.fromfile(root/f'block{block}-outview.fp8',np.uint8)
assert not np.any(plain[count:]) and not np.any(out[count:])
assert not np.any((out[:count]&127)==127)
inverse=np.argsort(np.load(f'release/native-c{C}/'+('split-view' if C==512 else 'view')+'/mapping.npz')['cell_output_to_hwc'])
target=e4m3fn(plain[:count].reshape(-1,16*C)[:,inverse]).reshape(size//4,size//4,4,4,C).transpose(0,2,1,3,4).reshape(size,size,C)
c=np.arange(C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
got=e4m3fn(out[:count]).reshape(C//16,size,size,16).transpose(1,2,0,3).reshape(size,size,C)[...,perm]
different=int(np.count_nonzero(got!=target))
report={'status':'pass' if different==0 else 'fail','different':different,'values':count,
        'scope':f'original block{block} outview versus plain in HWC'}
(root/f'block{block}-outview-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
assert different==0,'outview differs'
