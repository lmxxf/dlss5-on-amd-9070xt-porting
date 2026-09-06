"""Triangulate preblock CPU/AMD/original outputs without changing tolerances."""
from pathlib import Path
import argparse
import json
import numpy as np
from preblock_mix_reference import inputs
from native_c32_reference import block,H,F
from decode_tinlayout_global import e4m3fn

root=Path('release/native-rgb128')
parser=argparse.ArgumentParser();parser.add_argument('--cuda-noise',action='store_true');parser.add_argument('--seed',type=lambda s:int(s,0),default=0);parser.add_argument('--gpu-prefix',default='pre');args=parser.parse_args()
fw=np.fromfile(root/'amd/block0-ffn.f32','<f4')
aw=np.fromfile(root/'amd/block0-attention.f32','<f4')
rgb=np.fromfile(root/'input-hwc.rgba32f','<f4').reshape(128,128,4)
features=inputs(rgb[...,:3],seed=args.seed,live=True)
if args.cuda_noise:
 # Diagnostic isolation only: never feed captured activations to the GPU chain.
 features[...,[0,1,4]]=H(np.fromfile(root/f'noise-residual/trace-seed{args.seed}.f32','<f4').reshape(128,128,16)[...,[14,15,13]])
prefix=H(features@fw[:512].reshape(32,16).T)
tiles=prefix.reshape(16,8,16,8,32).transpose(0,2,1,3,4).reshape(256,64,32)
w=(fw[512:4608].reshape(128,32),fw[4608:8704].reshape(32,128),
   *aw[:4096].reshape(4,32,32),aw[4096:8192].reshape(64,64),
   aw[8192],fw[8704:],aw[8193:])
raw=block(tiles,w,raw_output=True).reshape(256,8,8,32)
gpu=np.fromfile(root/f'amd/{args.gpu_prefix}-raw.f32','<f4').reshape(raw.shape)
def report(name,a,b):
 err=np.abs(a-b);where=np.argwhere(err!=0)
 print(json.dumps({'comparison':name,'different':len(where),'values':a.size,
  'max_error':float(err.max()),'first_positions':where[:12].tolist()}),flush=True)
report('CPU raw vs AMD raw',raw,gpu)
packed=np.fromfile(root/f'noise-residual/original-seed{args.seed}-main.fp8' if args.seed else root/'block0-main.fp8',np.uint8)
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
original_main=np.empty_like(raw)
for ty in range(16):
 for tx in range(16):
  base=ty*16*2048+tx*1024
  record=np.concatenate([packed[base:base+1024],packed[base+16*1024:base+17*1024]])
  original_main[ty*16+tx]=e4m3fn(record[pm]).reshape(8,8,32)
report('CPU main vs original main',F(raw),original_main)
def pool(t):
 rows=H(t[:,:,::2]+t[:,:,1::2])
 return F(H(H(rows[:,::2]+rows[:,1::2])*.25)).reshape(16,16,4,4,32).transpose(0,2,1,3,4).reshape(64,64,32)
original=e4m3fn(np.fromfile(root/f'noise-residual/original-seed{args.seed}-down.fp8' if args.seed else root/'block0-ds.fp8',np.uint8)).reshape(2,64,64,16).transpose(1,2,0,3).reshape(64,64,32)
report('CPU DS vs original DS',pool(raw),original)
report('AMD raw pooled on CPU vs original DS',pool(gpu),original)
down=np.fromfile(root/f'amd/{args.gpu_prefix}-down.f32','<f4').reshape(original.shape)
report('AMD raw pooled on CPU vs AMD DS',pool(gpu),down)
if args.seed==0:
 chain=np.fromfile(root/'amd/audit-ds0.f32','<f4').reshape(original.shape)
 report('isolated AMD DS vs full-chain AMD DS',down,chain)
