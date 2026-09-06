"""Compare original RTX variable-token QKV to logical native math."""
from pathlib import Path
import argparse,json
import numpy as np
from native_split_reference import bits
from native_vit_qkv_reference import unpack,qkv
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--input-fixture',type=Path,required=True);a=p.parse_args()
x=np.load(a.input_fixture/'logical.npz')['q'];n=x.shape[0]
expected=qkv(x,*unpack(Path('release/native-vit/block31-qkv.weights')))
report={'tokens':n,'scope':'isolated original QKV; not game acceptance','parts':[]}
for part in range(3):
 raw=np.fromfile(a.folder/f'qkv-1-{part}.fp8',np.uint8)
 tb=([1,0,4,5,2,15] if part==2 else [3 if part==1 else 2,6,7,8,14,15])+list(range(16,10+n.bit_length()-1))
 cb=[6,3,9,7,8,10,11,12,13,14] if part==2 else [0,1,2 if part==1 else 3,4,5,9,10,11,12,13]
 actual=np.empty_like(x);actual[bits(n*1024,tb),bits(n*1024,cb)]=e4m3fn(raw[:n*1024])
 delta=np.abs(expected[part]-actual)
 report['parts'].append({'part':part,'different':int(np.count_nonzero(delta)),'max_abs':float(delta.max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected[part]).all()),'tail_zero':not bool(raw[n*1024:].any()),'replay_identical':(a.folder/f'qkv-1-{part}.fp8').read_bytes()==(a.folder/f'qkv-2-{part}.fp8').read_bytes()})
report['pass']=all(s['different']==0 and s['finite'] and s['tail_zero'] and s['replay_identical'] for s in report['parts'])
print(json.dumps(report,indent=2));(a.folder/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass']
