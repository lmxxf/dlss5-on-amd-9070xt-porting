"""Compare original five TEX returns to current interpolation candidate."""
from pathlib import Path
import json,argparse
import numpy as np
from native_temporal_sampling_reference import bilinear,texture_float32
parser=argparse.ArgumentParser();parser.add_argument('--valid1080',action='store_true');parser.add_argument('--capture',default='block4-row6-warp1');args=parser.parse_args()
root=Path('release/native-temporal-valid1080' if args.valid1080 else 'release/native-temporal-large');cap=root/args.capture
width,height=(1920,1080) if args.valid1080 else (120,72)
def regs(pc,indices):
    rows=json.loads((cap/f'sample-registers-{pc}.json').read_text())['rows']
    return np.array([[r['raw'][str(k)] for k in indices] for r in rows],np.uint32).view(np.float32)
uv=regs('1590',[48,49,44,45,54,45,60,61,56,45]).reshape(32,5,2)
actual=np.stack([regs(pc,ks) for pc,ks in [('16b0',[48,49,53]),('16f0',[44,45,52]),
    ('1730',[54,55,58]),('1770',[60,61,59]),('17a0',[56,57,62])]],axis=1)
xy=np.floor(uv.astype(np.float64)*2**21)/2**21*[width,height]
history=np.fromfile(root/'history.f32',np.float32).reshape(height,width,4)[:,:,:3]
expected=texture_float32(bilinear(history,xy,8,8))
error=np.abs(actual-expected)
bad=np.argwhere(np.max(error,axis=2)>0)
report={'scope':'original five TEX returns vs current interpolation candidate, one warp',
        'max_abs':float(error.max()),'large_error_taps':[
            {'lane':int(i),'tap':int(j),'uv':uv[i,j].tolist(),
             'uv_bits':[hex(int(x)) for x in uv[i,j].view(np.uint32)],
             'tex':actual[i,j].tolist(),'candidate':expected[i,j].tolist()} for i,j in bad]}
(cap/'texture-returns-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
