"""Uniform-QK, varying-V attention control for64/128/256 tokens."""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from native_c32_reference import H,F
from native_split_reference import bits
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('--tokens',type=int,choices=[64,128,256],required=True);p.add_argument('--seed',type=int,default=2901);p.add_argument('--reuse',action='store_true');a=p.parse_args()
n=a.tokens;root=Path('release/native-vit')/f'uniform-qk-{n}-{a.seed}'
if not a.reuse:root.mkdir(exist_ok=False)
report={'status':'running','tokens':n,'seed':a.seed,'scope':'original varying-V uniform-QK control; does not prove arbitrary attention or token ordering'}
def save():(root/('validation-exp.json' if a.reuse else 'validation.json')).write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    v=np.random.default_rng(a.seed).integers(-2,3,(n,1024)).astype(np.float32)*.25
    high=list(range(16,10+n.bit_length()-1))
    vt=bits(n*1024,[1,0,4,5,2,15]+high);vc=bits(n*1024,[6,3,9,7,8,10,11,12,13,14])
    assert np.unique(vt*1024+vc).size==n*1024
    packed=np.zeros(((n+127)//128*128)*2048,np.uint8);packed[:n*1024]=quantize(v)[vt,vc]
    if a.reuse:
        assert (root/'v.fp8').read_bytes()==packed.tobytes() and not any((root/'q.fp8').read_bytes()) and not any((root/'k.fp8').read_bytes())
    else:
        packed.tofile(root/'v.fp8');(root/'q.fp8').write_bytes(bytes(((n+127)//128*128)*1024));(root/'k.fp8').write_bytes((root/'q.fp8').read_bytes())
    width=8 if n==64 else 16;height=n//width
    if not a.reuse:subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-vit-attention-oracle',str(root/'q.fp8'),str(root/'k.fp8'),str(root/'v.fp8'),str(root/'output.fp8'),str(width),str(height),'32'],check=True,timeout=20)
    raw=np.fromfile(root/'output.fp8',np.uint8);assert not np.any(raw[n*1024:]) and not np.any((raw[:n*1024]&127)==127)
    ot=bits(n*1024,[2,6,7,8,14,15]+high);oc=bits(n*1024,[0,1,3,4,5,9,10,11,12,13])
    got=np.empty((n,1024),np.float32);got[ot,oc]=e4m3fn(raw[:n*1024])
    affine=H(np.array([1.708984375],np.float32)).astype(np.float16).view(np.uint16).astype(np.uint32)
    exp=(((affine<<4)+0x4000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)[0]
    numerator=np.zeros(1024,np.float32)
    quantized_exp=F(np.array([exp],np.float32))[0]
    for start in range(0,n,32):numerator=H(numerator+np.sum(v[start:start+32]*quantized_exp,axis=0))
    expected=np.broadcast_to(F(H(numerator*H(1/H(exp*n)))),got.shape)
    err=np.abs(got-expected);report.update(values=int(got.size),different=int(np.count_nonzero(err)),max_error=float(err.max()))
    report['queries_identical']=bool(np.all(got==got[:1]))
    report['different_per_32_queries']=[int(np.count_nonzero(err[i:i+32])) for i in range(0,n,32)]
    assert report['different']==0,'uniform QK channel mean control differs'
    report['status']='control_pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
