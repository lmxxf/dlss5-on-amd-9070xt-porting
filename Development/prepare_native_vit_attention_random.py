"""Prepare finite random Q/K/V logical and candidate physical layouts."""
from pathlib import Path
import argparse,json
import numpy as np
from native_c32_reference import F
from native_split_reference import bits
from encode_tinlayout_global import quantize
p=argparse.ArgumentParser();p.add_argument('--tokens',type=int,choices=[64,128,256,640],required=True);p.add_argument('--seed',type=int,default=3001);a=p.parse_args()
n=a.tokens;root=Path('release/native-vit')/f'attention-random-{n}-{a.seed}';root.mkdir(exist_ok=False)
rng=np.random.default_rng(a.seed);values=[F(rng.normal(0,s,(n,1024)).astype(np.float32)) for s in (.5,.25,.5)]
high=list(range(16,10+(n-1).bit_length()))
for part,(name,value) in enumerate(zip(('q','k','v'),values)):
    tb=([1,0,4,5,2,15] if part==2 else [3 if part==1 else 2,6,7,8,14,15])+high
    cb=[6,3,9,7,8,10,11,12,13,14] if part==2 else [0,1,2 if part==1 else 3,4,5,9,10,11,12,13]
    t,c=bits(n*1024,tb),bits(n*1024,cb);assert np.unique(t*1024+c).size==n*1024
    packed=np.zeros(((n+127)//128*128)*(2048 if part==2 else 1024),np.uint8);packed[:n*1024]=quantize(value)[t,c];packed.tofile(root/f'{name}.fp8')
np.savez(root/'logical.npz',q=values[0],k=values[1],v=values[2])
(root/'fixture.json').write_text(json.dumps({'tokens':n,'seed':a.seed,'scope':'random attention fixture; original numerical validation pending'},indent=2)+'\n')
