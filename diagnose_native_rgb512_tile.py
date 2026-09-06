"""Isolate the first divergent preblock window without fitting corrections."""
from pathlib import Path
import subprocess,json,argparse
import numpy as np
from preblock_mix_reference import inputs
from native_c32_reference import block,H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--float32-before-half',action='store_true');args=parser.parse_args()
root=Path('release/native-rgb512');tx,ty=48,47
subprocess.run(['/tmp/probe-native-noise',str(root/'noise-trace.f32'),'512','512','0'],check=True)
rgb=np.memmap(root/'input-hwc.rgba32f',dtype='<f4',mode='r',shape=(512,512,4))[ty*8:ty*8+8,tx*8:tx*8+8]
noise=np.memmap(root/'noise-trace.f32',dtype='<f4',mode='r',shape=(512,512,16))[ty*8:ty*8+8,tx*8:tx*8+8]
fw=np.fromfile(root/'amd/block0-ffn.f32','<f4');aw=np.fromfile(root/'amd/block0-attention.f32','<f4')
features=inputs(rgb[...,:3],seed=0,live=True);features[...,[0,1,4]]=H(noise[...,[14,15,13]])
accumulated=features.astype(np.float64)@fw[:512].reshape(32,16).astype(np.float64).T
prefix=H(accumulated.astype(np.float32) if args.float32_before_half else accumulated)
w=(fw[512:4608].reshape(128,32),fw[4608:8704].reshape(32,128),*aw[:4096].reshape(4,32,32),aw[4096:8192].reshape(64,64),aw[8192],fw[8704:],aw[8193:])
raw=block(prefix.reshape(1,64,32),w,raw_output=True).reshape(8,8,32)
packed=np.memmap(root/'block0-main.fp8',dtype=np.uint8,mode='r');base=ty*64*2048+tx*1024
record=np.concatenate([packed[base:base+1024],packed[base+64*1024:base+65*1024]])
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
main=e4m3fn(record[pm]).reshape(8,8,32)
dsraw=np.memmap(root/'block0-ds.fp8',dtype=np.uint8,mode='r',shape=(2,256,256,16))[:,ty*4:ty*4+4,tx*4:tx*4+4]
down=e4m3fn(np.asarray(dsraw)).transpose(1,2,0,3).reshape(4,4,32)
rows=H(raw[:,::2]+raw[:,1::2]);pooled=F(H(H(rows[::2]+rows[1::2])*.25))
gpu=np.memmap(root/'amd/audit-ds0.f32',dtype='<f4',mode='r',shape=(256,256,32))[ty*4:ty*4+4,tx*4:tx*4+4]
for name,a,b in [('CPU-main/original',F(raw),main),('CPU-DS/original',pooled,down),('CPU-DS/AMD',pooled,gpu)]:
 error=np.abs(a-b);where=np.argwhere(error!=0)
 print(json.dumps({'comparison':name,'different':len(where),'max_error':float(error.max()),'first_mismatches':[{'position':p.tolist(),'cpu':float(a[tuple(p)]),'other':float(b[tuple(p)])} for p in where[:12]]}),flush=True)
np.savez(root/'tile-diagnostic.npz',features=features,prefix=prefix,raw=raw,original_main=main,original_down=down,gpu_down=np.asarray(gpu),float32_before_half=args.float32_before_half)
