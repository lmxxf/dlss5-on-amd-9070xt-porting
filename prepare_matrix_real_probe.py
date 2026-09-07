"""Pack original block52 expansion weights and real chain inputs, losslessly."""
from pathlib import Path
import argparse, hashlib, json
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--row',type=int,default=0);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert 0<=a.row<=992 and a.row%32==0
root=Path('release/native-color-frame/network/decoder-c256/decoder-block52')
w=np.fromfile(root/'ffn.f32','<f4')[:1024*256].reshape(1024,256)[a.row:a.row+32]
x=np.fromfile(root/'input.f32','<f4').reshape(-1,256)
indices=(np.arange(256)*31)%len(x);x=x[indices]
assert np.isfinite(w).all() and np.isfinite(x).all()
assert np.array_equal(w,w.astype('<f2').astype('<f4')) and np.array_equal(x,x.astype('<f2').astype('<f4'))
packed=w.reshape(32,8,32).transpose(1,0,2).astype('<f2').tobytes()+x.astype('<f2').tobytes()
assert len(packed)==147456
a.output.parent.mkdir(parents=True,exist_ok=True)
if a.output.exists():raise RuntimeError('refusing fixture overwrite')
a.output.write_bytes(packed)
a.output.with_suffix('.json').write_text(json.dumps(dict(scope='block52 32 expansion rows, 256 selected pixels, full K256; not whole layer',row=a.row,indices=indices.tolist(),sha256=hashlib.sha256(packed).hexdigest(),sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [root/'ffn.f32',root/'input.f32']}),indent=2)+'\n')
