"""Original-byte coefficients and independently decoded0..4 oracles for AMD."""
from pathlib import Path
import numpy as np,json
from native_c32_reference import unpack
from decode_tinlayout_global import e4m3fn
base=Path('release/native-rgb-valid1080');src=base/'encoder-c32';out=base/'amd-front4';out.mkdir(exist_ok=False)
r=json.loads((src/'main-validation.json').read_text());assert len(r['checks'])==4 and all(c['different']==0 and c['finite'] for c in r['checks'])
r=json.loads((src/'down-validation-corrected.json').read_text());assert r['different']==0 and r['finite']
for name in ('ffn','attention'):(out/f'block0-{name}.f32').write_bytes((Path('release/native-rgb512/amd')/f'block0-{name}.f32').read_bytes())
for b in range(1,5):
 w1,w2,q,k,v,p,bias,scale,fs,ats=unpack(src/f'block{b}.weights')
 np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(out/f'block{b}-ffn.f32')
 np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(out/f'block{b}-attention.f32')
layout=np.load('release/preblock-ffn-byte-layout/layout.npz');raw=np.fromfile(src/'block4.weights',np.uint8)
matrix=np.empty((64,32),np.float32);matrix[layout['w1_hidden'][:2048],layout['w1_input'][:2048]]=e4m3fn(raw[20656:22704]);matrix.tofile(out/'block4-ds.f32')
codes=np.fromfile('release/native-c32/ds-coded-aux.fp8',np.uint8)[:32768].reshape(4,16,32,16).transpose(1,2,0,3).reshape(16,32,64)
order=codes[0,0].astype(np.int32)-8;assert sorted(order.tolist())==list(range(64)) and np.all(codes==8+order)
physical=np.fromfile(src/'block4-down.f32',np.float32).reshape(288,480,64)
physical[...,np.argsort(order)].copy().tofile(out/'oracle-down.f32')
(out/'oracle-main4.f32').write_bytes((src/'block4-main.f32').read_bytes())
(out/'input.f32').write_bytes((base/'input-hwc.rgba32f').read_bytes())
(out/'provenance.json').write_text(json.dumps({'scope':'valid1080 single RGB0..4 fixture; GPU chain pending','down_order':'matrix output rows, inverse of independently coded physical channel order','main_shape':[576,960,32],'down_shape':[288,480,64]},indent=2)+'\n')
