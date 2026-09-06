"""Original block31 full sequential reference comparison at 256 tokens."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
from native_vit_linear_reference import expand,unpack_expand,unpack_residual,residual_projection
from native_vit_qkv_reference import qkv,unpack
from native_vit_attention_reference import attention
root=Path('release/native-vit/block256-3003');base=Path('release/native-vit');n=256;reports=[]
def check(name,expected,part=None):
 c=expected.shape[1];size=n*c;b=c.bit_length()
 tb=[2,6,7,8,b+3,b+4,b+5,b+6];cb=[0,1,3,4,5]+list(range(9,b+3))
 if part==1:tb=[3,6,7,8,14,15,16,17];cb=[0,1,2,4,5,9,10,11,12,13]
 if part==2:tb=[1,0,4,5,2,15,16,17];cb=[6,3,9,7,8,10,11,12,13,14]
 raw=np.fromfile(root/f'trial-1-{name}.fp8',np.uint8);actual=np.empty_like(expected);actual[bits(size,tb),bits(size,cb)]=e4m3fn(raw[:size])
 r={'stage':name,'different':int(np.count_nonzero(actual!=expected)),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all()),'tail_zero':not bool(raw[size:].any()),'replay_identical':(root/f'trial-1-{name}.fp8').read_bytes()==(root/f'trial-2-{name}.fp8').read_bytes()};reports.append(r);print(r,flush=True)
 (root/'validation.json').write_text(json.dumps({'scope':'original full block256, not AMD/game','stages':reports},indent=2)+'\n')
 assert r['different']==0 and r['finite'] and r['tail_zero'] and r['replay_identical']
 return actual
x=np.load(base/'attention-random-256-3003/logical.npz')['q']
ew=unpack_expand(base/'block31-expand.weights');cw,cs=unpack_residual(base/'block31-contract.weights',4096);qw,qs=unpack(base/'block31-qkv.weights');pw,ps=unpack_residual(base/'block31-projection.weights',1024)
e=check('expand',expand(x,ew));c=check('contract',residual_projection(e,x,cw,cs))
vectors=qkv(c,qw,qs)
for i in range(3):check(f'qkv-{i}',vectors[i],i)
a=check('attention',attention(*vectors));out=check('projection',residual_projection(a,c,pw,ps))
x.tofile(root/'input.f32');out.tofile(root/'oracle.f32')
for name,value in [('expand',ew.ravel()),('contract',np.concatenate([cw.ravel(),cs])),('qkv',np.concatenate([*[m.ravel() for m in qw],qs])),('projection',np.concatenate([pw.ravel(),ps]))]:value.astype('<f4').tofile(root/f'{name}.f32')
