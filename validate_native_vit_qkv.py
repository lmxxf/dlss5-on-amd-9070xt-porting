"""Strict per-coordinate Q/K/V validation on the current 16-token fixture."""
from pathlib import Path
import argparse,os,subprocess,json
import numpy as np
from native_split_reference import bits
from native_vit_qkv_reference import unpack,qkv
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--folder',type=Path,default=Path('release/native-vit'));args=parser.parse_args();root=args.folder
path=Path('release/native-vit/block31-qkv.weights')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_VIT_QKV_')}
subprocess.run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(path),str(root/'qkv-reference'),'4','4','16'],check=True,env=env)
source=np.fromfile(root/'contract.fp8',np.uint8);assert not np.any(source[16384:]) and not np.any((source[:16384]&127)==127)
x=np.empty((16,1024),np.float32);x[bits(16384,[2,6,7,8]),bits(16384,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(source[:16384])
predictions=qkv(x,*unpack(path));all_exact=True
for part,predicted in enumerate(predictions):
 raw=np.fromfile(root/f'qkv-reference-{part}.fp8',np.uint8);count=32768 if part==2 else 16384
 assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 if part==2:
  valid=np.flatnonzero((np.arange(count)&4)==0);assert not np.any(raw[np.flatnonzero((np.arange(count)&4)!=0)])
  t=bits(count,[1,0,4,5])[valid];c=bits(count,[6,3,9,7,8,10,11,12,13,14])[valid]
 else:
  valid=np.arange(count);t=bits(count,[3 if part else 2,6,7,8]);c=bits(count,[0,1,2 if part else 3,4,5,9,10,11,12,13])
 assert np.unique(t*1024+c).size==16384
 target=np.empty_like(predicted);target[t,c]=e4m3fn(raw[valid]);error=np.abs(predicted-target)
 different=int(np.count_nonzero(error));all_exact &= different==0
 print(json.dumps({'part':'QKV'[part],'values':16384,'different':different,'max_error':float(error.max()),'fixture':str(root)}),flush=True)
assert all_exact,'native QKV differs from original'
