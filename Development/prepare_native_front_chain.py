"""Prepare original-byte-derived parameters for native block0..4 GPU tests."""
from pathlib import Path
import argparse,json,hashlib
import numpy as np
from native_c32_reference import unpack
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
for name,source in [('block0-ffn.f32','release/preblock-ffn-amd/weights.f32'),('block0-attention.f32','release/preblock-attention-amd/weights.f32'),('input.rgba32f','release/preblock-global/input-tiles.rgba32f')]:
 (a.output/name).write_bytes(Path(source).read_bytes())
for i in [1,2,3,4]:
 w1,w2,q,k,v,pw,bias,scale,fs,ats=unpack(f'release/native-c32/block{i}.weights')
 np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(a.output/f'block{i}-ffn.f32')
 np.concatenate([q.ravel(),k.ravel(),v.ravel(),pw.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(a.output/f'block{i}-attention.f32')
from decode_tinlayout_global import e4m3fn
layout=np.load('release/preblock-ffn-byte-layout/layout.npz')
raw=np.fromfile('release/native-c32/block4.weights',np.uint8);assert raw.size==22720
matrix=np.empty((64,32),np.float32);matrix[layout['w1_hidden'][:2048],layout['w1_input'][:2048]]=e4m3fn(raw[20656:22704])
matrix.astype('<f4').tofile(a.output/'block4-ds.f32')
fw=np.load('release/native-c64/ffn-layout/matrices.npz');aw=np.load('release/native-c64/attention-layout/full-matrices.npz')
np.concatenate([fw[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(a.output/'block5-ffn.f32')
np.concatenate([aw[k].ravel() for k in ('Q','K','V','P','bias','scales','skip')]).astype('<f4').tofile(a.output/'block5-attention.f32')
from native_c64_reference import unpack as unpack64
for i in range(6,23):
 fw,qkv,pw,bias,scales,skip=unpack64(f'release/native-c{256 if i>=15 else 128 if i>=9 else 64}/block{i}.weights')
 np.concatenate([fw[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(a.output/f'block{i}-ffn.f32')
 np.concatenate([m.ravel() for m in (*qkv,pw,bias,scales,skip)]).astype('<f4').tofile(a.output/f'block{i}-attention.f32')
layout64=np.load('release/native-c64/ffn-layout/layout.npz')
raw8=np.fromfile('release/native-c64/block8.weights',np.uint8);assert raw8.size==69936
ds8=np.empty((128,64),np.float32);ds8[layout64['w1_hidden'][:8192],layout64['w1_input'][:8192]]=e4m3fn(raw8[0xf130:])
ds8.astype('<f4').tofile(a.output/'block8-ds.f32')
layout128=np.load('release/native-c128/ds-layout/layout.npz')
raw14=np.fromfile('release/native-c128/block14.weights',np.uint8);assert raw14.size==229936
ds14=np.empty((256,128),np.float32);ds14[layout128['output'],layout128['input']]=e4m3fn(raw14[0x30230:])
ds14.astype('<f4').tofile(a.output/'block14-ds.f32')
layout256=np.load('release/native-c256/ds-layout/layout.npz')
raw22=np.fromfile('release/native-c256/block22.weights',np.uint8);assert raw22.size==820288
ds22=np.empty((512,256),np.float32);ds22[layout256['output'],layout256['input']]=e4m3fn(raw22[0xa8440:])
ds22.astype('<f4').tofile(a.output/'block22-ds.f32')
manifest={'input_extent':[128,64],'body_extent':[64,32],'shift_masks':[0,3,1,2],'ds_extent':[32,16],'files':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.output.glob('*.f32')}}
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
