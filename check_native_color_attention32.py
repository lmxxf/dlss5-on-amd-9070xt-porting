"""Isolate new color-frame attention32 mismatch using original Q/K/V outputs."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
from native_vit_attention_reference import attention
root=Path('release/native-color-frame/network/vit/block32')
n=640;c=1024;size=n*c
def decode(name,part=None):
    # Match check_native_block256's b=bit_length(1024)=11 convention.
    tb=[2,6,7,8,14,15,16,17,18,19]
    cb=[0,1,3,4,5,9,10,11,12,13]
    if part==1:tb=[3,6,7,8,14,15,16,17,18,19];cb=[0,1,2,4,5,9,10,11,12,13]
    if part==2:tb=[1,0,4,5,2,15,16,17,18,19];cb=[6,3,9,7,8,10,11,12,13,14]
    raw=np.fromfile(root/f'trial-1-{name}.fp8',np.uint8)
    assert raw.size>=size and not raw[size:].any()
    out=np.empty((n,c),np.float32);out[bits(size,tb),bits(size,cb)]=e4m3fn(raw[:size]);return out
q,k,v=[decode(f'qkv-{i}',i) for i in range(3)]
actual=decode('attention');expected=attention(q,k,v,experimental_640=True)
indices=np.argwhere(actual!=expected)
report=dict(scope='isolated original attention32 vs CPU; not accepted',different=len(indices),
 values=[dict(token=int(t),channel=int(c),original=float(actual[t,c]),cpu=float(expected[t,c])) for t,c in indices])
(root/'attention-mismatch.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
from native_c32_reference import H,F
from native_c32_softmax_sum import denominator
for t,ch in indices:
    head,lane=int(ch)//32,int(ch)%32
    score=H(q[t,head*32:head*32+32]@k[:,head*32:head*32+32].T)
    coef=np.array([0x2dbb],np.uint16).view(np.float16).astype(np.float32)[0]
    affine=np.clip(H(score*coef+np.float32(1.708984375)),1.439453125,1.9775390625)
    b=affine.astype(np.float16).view(np.uint16).astype(np.uint32)
    exp=(((b<<4)+0x4000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
    order=np.zeros(64,np.int32)
    for bit,dest in enumerate([4,0,1,3,2,5]):order|=((np.arange(64)>>bit)&1)<<dest
    den=denominator(exp[:64][np.argsort(order)].reshape(1,64))
    for start in range(64,n,64):den=H(den+denominator(exp[start:start+64][np.argsort(order)].reshape(1,64)))
    acc=np.float32(0)
    for start in range(0,n,32):
        exact=np.sum(F(exp[start:start+32]).astype(np.float64)*v[start:start+32,ch].astype(np.float64))+float(acc)
        acc=H(exact)
    print('double_product_accum',float(F(H(acc*H(1/den))).item()),'numerator',float(acc),'denominator',float(den.item()))
