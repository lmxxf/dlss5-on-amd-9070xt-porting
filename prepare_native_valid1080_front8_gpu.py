"""Same valid1080 RGB0..8 coefficients and original edge outputs."""
from pathlib import Path
import json,numpy as np
from native_c64_reference import unpack
base=Path('release/native-rgb-valid1080');src=base/'encoder-c64';out=base/'amd-front8';out.mkdir(exist_ok=False)
r=json.loads((src/'main-validation.json').read_text());assert len(r['checks'])==4 and all(c['different']==0 and c['finite'] for c in r['checks'])
r=json.loads((src/'down-validation.json').read_text());assert r['different']==0 and r['finite']
for f in (base/'amd-front4').glob('block*.f32'):(out/f.name).write_bytes(f.read_bytes())
(out/'input.f32').write_bytes((base/'input-hwc.rgba32f').read_bytes())
for b in range(5,9):
 f,qkv,p,bias,sc,skip=unpack(src/f'block{b}.weights')
 np.concatenate([f[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(out/f'block{b}-ffn.f32')
 np.concatenate([*[m.ravel() for m in qkv],p.ravel(),bias.ravel(),sc,skip]).astype('<f4').tofile(out/f'block{b}-attention.f32')
for a,b in [('block8-ds.f32','block8-ds.f32'),('block8-main.f32','oracle-main8.f32'),('block8-down.f32','oracle-down.f32')]:(out/b).write_bytes((src/a).read_bytes())
(out/'provenance.json').write_text(json.dumps({'scope':'same single valid1080 RGB0..8; GPU test pending','main_shape':[288,480,64],'down_shape':[144,240,128]},indent=2)+'\n')
