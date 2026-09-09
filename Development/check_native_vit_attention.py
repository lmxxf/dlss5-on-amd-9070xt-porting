"""Explicit native attention arithmetic candidates; requires exact coordinates."""
from pathlib import Path
import subprocess,json,argparse
import numpy as np
from native_split_reference import bits
from native_vit_qkv_reference import unpack,qkv
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
from native_c32_softmax_sum import denominator
parser=argparse.ArgumentParser();parser.add_argument('--tokens',type=int,choices=[16,64],default=16);parser.add_argument('--seed',type=int,default=2017);args=parser.parse_args();N=args.tokens
if N==16 and args.seed!=2017:parser.error('seed selects synthetic 64-token fixtures only')
base=Path('release/native-vit');root=base if N==16 else base/f'attention64-{args.seed}';root.mkdir(exist_ok=True)
t=bits(N*1024,[2,6,7,8]+([] if N==16 else [14,15]));c=bits(N*1024,[0,1,3,4,5,9,10,11,12,13])
if N==16:
 source=np.fromfile(root/'contract.fp8',np.uint8);x=np.empty((16,1024),np.float32);x[t,c]=e4m3fn(source[:16384])
else:x=F(np.random.default_rng(args.seed).normal(0,1,(N,1024)).astype(np.float32))
vectors=qkv(x,*unpack(base/'block31-qkv.weights'))
if N==64:
 for part,a in enumerate(vectors):
  tb=[1,0,4,5,2,15] if part==2 else [3 if part else 2,6,7,8,14,15]
  cb=[6,3,9,7,8,10,11,12,13,14] if part==2 else [0,1,2 if part else 3,4,5,9,10,11,12,13]
  quantize(a)[bits(N*1024,tb),bits(N*1024,cb)].tofile(root/f'qkv-reference-{part}.fp8')
subprocess.run(['/tmp/native-vit-attention-oracle',*[str(root/f'qkv-reference-{i}.fp8') for i in range(3)],str(root/'attention.fp8'),'4' if N==16 else '8','4' if N==16 else '8','32'],check=True)
q,k,v=[a.reshape(N,32,32).transpose(1,0,2) for a in vectors]
score=H(q@k.transpose(0,2,1));coef=np.array([0x2dbb],np.uint16).view(np.float16).astype(np.float32)[0]
affine=np.clip(H(score*coef+np.float32(1.708984375)),1.439453125,1.9775390625)
exponent=(((affine.astype(np.float16).view(np.uint16).astype(np.uint32)<<4)+0x4000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
raw=np.fromfile(root/'attention.fp8',np.uint8);assert not np.any(raw[N*1024:]) and not np.any((raw[:N*1024]&127)==127)
target=np.empty((N,1024),np.float32);target[t,c]=e4m3fn(raw[:N*1024]);best=N*1024+1;native_exact=False
numerator=np.zeros((32,N,32),np.float32)
for start in range(0,N,32):numerator=H(numerator+F(exponent[...,start:start+32])@v[:,start:start+32])
for den_mode in ['float_sum','adjacent_tree','halves_tree']+(['native64','canonical64'] if N==64 else []):
 if den_mode=='canonical64':
  perm=np.zeros(64,np.int32)
  for bit,destination in enumerate([4,0,1,3,2,5]):perm|=((np.arange(64)>>bit)&1)<<destination
  den=denominator(exponent[...,np.argsort(perm)])
 elif den_mode=='native64':den=denominator(exponent)
 elif den_mode=='float_sum':den=H(exponent.sum(-1,keepdims=True))
 else:
  den=exponent.copy()
  while den.shape[-1]>1:
   size=den.shape[-1]//2;den=H(den[...,::2]+den[...,1::2]) if den_mode=='adjacent_tree' else H(den[...,:size]+den[...,size:])
 for mode in ['exp_fp8_then_divide','prob_fp8','half_prob']:
  inverse=H(1/den)
  result=H(numerator*inverse) if mode=='exp_fp8_then_divide' else H((F(H(exponent*inverse)) if mode=='prob_fp8' else H(exponent*inverse))@v)
  got=F(result).transpose(1,0,2).reshape(N,1024);different=int(np.count_nonzero(got!=target));best=min(best,different)
  if den_mode=='canonical64' and mode=='exp_fp8_then_divide':native_exact=different==0
  print(json.dumps({'denominator':den_mode,'mode':mode,'different':different,'sorted_different':int(np.count_nonzero(np.sort(got.ravel())!=np.sort(target.ravel()))),'max_error':float(np.max(np.abs(got-target)))}),flush=True)
assert native_exact,'native canonical64 exp-first attention arithmetic not closed'
from native_vit_attention_reference import attention
assert np.array_equal(attention(*vectors),target),'reusable attention reference differs'
