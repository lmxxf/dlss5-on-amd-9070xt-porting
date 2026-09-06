"""Original same-RGB encoder5..8 main comparisons; down8 remains separate."""
from pathlib import Path
import json,argparse
import numpy as np
from native_c64_reference import block,unpack
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group();g.add_argument('--c128',action='store_true');g.add_argument('--c256',action='store_true');args=p.parse_args()
base=Path('release/native-rgb-valid1080');root=base/('encoder-c256' if args.c256 else 'encoder-c128' if args.c128 else 'encoder-c64');h,w,C=(72,120,256) if args.c256 else (144,240,128) if args.c128 else (288,480,64);n=h*w*C
raw=np.fromfile(base/('encoder-c128/block14-down.fp8' if args.c256 else 'encoder-c64/block8-down.fp8' if args.c128 else 'encoder-c32/block4-down.fp8'),np.uint8)[:n]
c=np.arange(C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
x=e4m3fn(raw).reshape(C//16,h,w,16).transpose(1,2,0,3).reshape(h,w,C)[...,perm]
inv=np.argsort(np.load(f'release/native-c{C}/view/mapping.npz')['cell_output_to_hwc']);checks=[]
for b,s in (((15,0),(16,3),(17,1),(18,2),(19,0),(20,3),(21,1),(22,2)) if args.c256 else ((9,0),(10,3),(11,1),(12,2),(13,0),(14,3)) if args.c128 else ((5,0),(6,3),(7,1),(8,2))):
 raw=np.fromfile(root/f'block{b}-main.fp8',np.uint8);assert not raw[n:].any() and not np.any((raw[:n]&127)==127)
 actual=e4m3fn(raw[:n].reshape(-1,16*C)[:,inv]).reshape(h//4,w//4,4,4,C).transpose(0,2,1,3,4).reshape(h,w,C)
 px=4 if s&1 else 0;py=4 if s&2 else 0;hh=h+2*py;ww=w+2*px
 padded=np.pad(x,((py,py),(px,px),(0,0)));tiles=padded.reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
 expected=block(tiles,*unpack(root/f'block{b}.weights')).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(hh,ww,C)[py:py+h,px:px+w]
 r={'block':b,'different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max()),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())};checks.append(r);print(r,flush=True)
 if r['different']:
  locations=np.argwhere(actual!=expected)
  r['mismatches']=[{'yxc':idx.tolist(),'original':float(actual[tuple(idx)]),'reference':float(expected[tuple(idx)])} for idx in locations[:32]]
 (root/'main-validation.json').write_text(json.dumps({'scope':'original/CPU encoder main same RGB; downsample not checked','checks':checks},indent=2)+'\n');assert r['different']==0 and r['finite']
 actual.tofile(root/f'block{b}-main.f32');x=actual
