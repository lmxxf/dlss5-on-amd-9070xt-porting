"""Compare spatial decoder interpolation candidates against original output."""
from pathlib import Path
import argparse,json
import numpy as np
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
from native_decoder_entry_reference import unpack,project,decoder_entry
p=argparse.ArgumentParser();p.add_argument('fixture',type=Path);a=p.parse_args();root=a.fixture
report=json.loads((root/'validation.json').read_text())
assert report['case']=='spatial' and report['status']=='smoke_pass'
x=np.fromfile(root/'main-hwc.f32','<f4').reshape(8,8,1024)
skip=np.fromfile(root/'skip-hwc.f32','<f4').reshape(16,16,512)
matrix,scale=unpack(root/'weights.bin');main=project(x,matrix)
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
output=np.fromfile(root/'result.output.fp8',np.uint8);assert not np.any(output[131072:])
target=e4m3fn(output[:131072].reshape(-1,8192)[:,inverse]).reshape(4,4,4,4,512).transpose(0,2,1,3,4).reshape(16,16,512)
checks=[]
def check(name,v):
    got=F(H(v+skip*scale));error=np.abs(got-target)
    checks.append({'candidate':name,'different':int(np.count_nonzero(error)),'max_error':float(error.max())})
check('nearest',np.repeat(np.repeat(main,2,0),2,1))
coord=np.arange(16)/2-.25;lo=np.floor(coord).astype(int);hi=lo+1;t=(coord-lo).astype(np.float32)
lo=np.clip(lo,0,7);hi=np.clip(hi,0,7)
for first_axis in ('x','y'):
    for rounded in (False,True):
        rounder=H if rounded else lambda v:v
        if first_axis=='x':
            row=rounder(main[:,lo]*(1-t)[None,:,None]+main[:,hi]*t[None,:,None])
            up=rounder(row[lo]*(1-t)[:,None,None]+row[hi]*t[:,None,None])
        else:
            col=rounder(main[lo]*(1-t)[:,None,None]+main[hi]*t[:,None,None])
            up=rounder(col[:,lo]*(1-t)[None,:,None]+col[:,hi]*t[None,:,None])
        check(f'bilinear-{first_axis}-first-half-{rounded}',up)
got=decoder_entry(x,skip,(matrix,scale))
exact=np.array_equal(got,target)
result={'status':'pass' if exact else 'fail','scope':'CPU/original block39 spatial 8x8 only; not AMD or game',
        'values':int(target.size),'different':int(np.count_nonzero(got!=target)),'checks':checks}
(root/'spatial-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
assert exact,'native decoder reference differs from original'
