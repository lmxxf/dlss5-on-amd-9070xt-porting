"""Constant-value controls to distinguish attention normalization from layout."""
from pathlib import Path
import subprocess,json
import numpy as np
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
from native_split_reference import bits
root=Path('release/native-vit');folder=root/'attention-controls';folder.mkdir(exist_ok=True)
zero=np.zeros(16384,np.uint8);ones=np.zeros(32768,np.uint8);ones[(np.arange(32768)&4)==0]=0x38
q=np.fromfile(root/'qkv-reference-0.fp8',np.uint8)[:16384];k=np.fromfile(root/'qkv-reference-1.fp8',np.uint8)[:16384];v=np.fromfile(root/'qkv-reference-2.fp8',np.uint8)[:32768]
for name,inputs in [('zero_qk_one_v',(zero,zero,ones)),('real_qk_one_v',(q,k,ones)),('zero_qk_real_v',(zero,zero,v))]:
 for i,a in enumerate(inputs):a.tofile(folder/f'{name}-{i}.fp8')
 path=folder/f'{name}-out.fp8'
 subprocess.run(['/tmp/native-vit-attention-oracle',*[str(folder/f'{name}-{i}.fp8') for i in range(3)],str(path),'4','4','32'],check=True)
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[16384:]) and not np.any((raw[:16384]&127)==127)
 values=e4m3fn(raw[:16384])
 print(json.dumps({'control':name,'min':float(values.min()),'max':float(values.max()),'unique_count':int(np.unique(values).size),'all_one':bool(np.all(values==1))}),flush=True)
for height in [4,8]:
 width=8;count=width*height*1024;name=f'zero_qk_one_v_{width}x{height}'
 for i,a in enumerate([np.zeros(count,np.uint8),np.zeros(count,np.uint8),np.full(count,0x38,np.uint8)]):a.tofile(folder/f'{name}-{i}.fp8')
 path=folder/f'{name}-out.fp8'
 subprocess.run(['/tmp/native-vit-attention-oracle',*[str(folder/f'{name}-{i}.fp8') for i in range(3)],str(path),str(width),str(height),'32'],check=True)
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 values=e4m3fn(raw[:count]);print(json.dumps({'control':name,'min':float(values.min()),'max':float(values.max()),'all_one':bool(np.all(values==1))}),flush=True)
name='random_qk_one_v_64';count=64*1024;rng=np.random.default_rng(2003)
tokens=bits(count,[2,6,7,8,14,15]);channels=bits(count,[0,1,3,4,5,9,10,11,12,13])
q=quantize(rng.normal(0,2,(64,1024)).astype(np.float32))[tokens,channels]
tokens=bits(count,[3,6,7,8,14,15]);channels=bits(count,[0,1,2,4,5,9,10,11,12,13])
k=quantize(rng.normal(0,.5,(64,1024)).astype(np.float32))[tokens,channels]
for i,a in enumerate([q,k,np.full(count,0x38,np.uint8)]):a.tofile(folder/f'{name}-{i}.fp8')
path=folder/f'{name}-out.fp8'
subprocess.run(['/tmp/native-vit-attention-oracle',*[str(folder/f'{name}-{i}.fp8') for i in range(3)],str(path),'8','8','32'],check=True)
raw=np.fromfile(path,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
values=e4m3fn(raw[:count]);print(json.dumps({'control':name,'min':float(values.min()),'max':float(values.max()),'all_one':bool(np.all(values==1))}),flush=True)
