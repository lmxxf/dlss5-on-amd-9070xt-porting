"""Prepare original-byte-derived parameters for the native block0..3 GPU test."""
from pathlib import Path
import argparse,json,hashlib
import numpy as np
from native_c32_reference import unpack
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
for name,source in [('block0-ffn.f32','release/preblock-ffn-amd/weights.f32'),('block0-attention.f32','release/preblock-attention-amd/weights.f32'),('input.rgba32f','release/preblock-global/input-tiles.rgba32f')]:
 (a.output/name).write_bytes(Path(source).read_bytes())
for i in [1,2,3]:
 w1,w2,q,k,v,pw,bias,scale,fs,ats=unpack(f'release/native-c32/block{i}.weights')
 np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(a.output/f'block{i}-ffn.f32')
 np.concatenate([q.ravel(),k.ravel(),v.ravel(),pw.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(a.output/f'block{i}-attention.f32')
manifest={'input_extent':[128,64],'body_extent':[64,32],'shift_masks':[0,3,1],'files':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.output.glob('*.f32')}}
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
