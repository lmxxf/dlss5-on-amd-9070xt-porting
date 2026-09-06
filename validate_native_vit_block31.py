"""Run a complete original 64-token block31, checking every CPU reference stage."""
from pathlib import Path
import argparse,subprocess,json,os
import numpy as np
from native_split_reference import bits
from native_c32_reference import F
from native_vit_linear_reference import unpack_expand,expand,unpack_residual,residual_projection
from native_vit_qkv_reference import unpack as unpack_qkv,qkv
from native_vit_attention_reference import attention
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
parser=argparse.ArgumentParser();parser.add_argument('--seed',type=int,default=2101);args=parser.parse_args()
base=Path('release/native-vit');root=base/f'block31-64-{args.seed}';root.mkdir(exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_VIT_')}
def run(argv):subprocess.run(argv,check=True,env=env)
def coordinates(C):
 return bits(64*C,[2,6,7,8,C.bit_length()+3,C.bit_length()+4]),bits(64*C,[0,1,3,4,5]+list(range(9,C.bit_length()+3)))
def decode(path,C=1024,part=None):
 n=64*C;raw=np.fromfile(path,np.uint8);assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 if part==2:t,c=bits(n,[1,0,4,5,2,15]),bits(n,[6,3,9,7,8,10,11,12,13,14])
 elif part==1:t,c=bits(n,[3,6,7,8,14,15]),bits(n,[0,1,2,4,5,9,10,11,12,13])
 else:t,c=coordinates(C)
 result=np.empty((64,C),np.float32);result[t,c]=e4m3fn(raw[:n]);return result
def compare(stage,predicted,path,C=1024,part=None):
 target=decode(path,C,part);error=np.abs(predicted-target)
 print(json.dumps({'stage':stage,'seed':args.seed,'values':target.size,'different':int(np.count_nonzero(error)),'max_error':float(error.max())}),flush=True)
 assert np.array_equal(predicted,target),stage+' differs'
x=F(np.random.default_rng(args.seed).normal(0,1,(64,1024)).astype(np.float32));t,c=coordinates(1024);quantize(x)[t,c].tofile(root/'input.fp8')
run(['/tmp/native-vit-expand-oracle',str(root/'input.fp8'),str(root/'expand.fp8'),str(base/'block31-expand.weights'),'64','32'])
branch=expand(x,unpack_expand(base/'block31-expand.weights'));compare('expand',branch,root/'expand.fp8',4096)
run(['/tmp/native-vit-contract-oracle',str(root/'expand.fp8'),str(root/'input.fp8'),str(base/'block31-contract.weights'),str(root/'contract.fp8'),'64','8'])
feature=residual_projection(branch,x,*unpack_residual(base/'block31-contract.weights',4096));compare('contract',feature,root/'contract.fp8')
run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(base/'block31-qkv.weights'),str(root/'qkv'),'8','8','16'])
vectors=qkv(feature,*unpack_qkv(base/'block31-qkv.weights'))
for part,a in enumerate(vectors):compare('QKV'[part],a,root/f'qkv-{part}.fp8',part=part)
run(['/tmp/native-vit-attention-oracle',*[str(root/f'qkv-{i}.fp8') for i in range(3)],str(root/'attention.fp8'),'8','8','32'])
attended=attention(*vectors);compare('attention',attended,root/'attention.fp8')
run(['/tmp/native-vit-contract-oracle',str(root/'attention.fp8'),str(root/'contract.fp8'),str(base/'block31-projection.weights'),str(root/'projection.fp8'),'64','8','projection'])
result=residual_projection(attended,feature,*unpack_residual(base/'block31-projection.weights',1024));compare('projection',result,root/'projection.fp8')
